#include "preview.h"
#include <cmath>
#include <deque>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <unistd.h>
#include <sys/syscall.h>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include "Frame.h"
#include "camera.h"


using namespace std::chrono;


extern std::atomic_bool end_program;

// AGC enable state
extern std::atomic_bool agc_enabled;

// AGC outputs (possibly under manual control here)
extern std::atomic_int camera_gain;
extern std::atomic_int camera_exposure_us;

// disk write state
extern std::atomic_bool disk_file_exists;
extern std::atomic_bool disk_write_enabled;

extern std::mutex to_preview_deque_mutex;
extern std::condition_variable to_preview_deque_cv;
extern std::deque<Frame *> to_preview_deque;

// trackbar positions
int gain_trackbar_pos;
int exposure_trackbar_pos;

constexpr char PREVIEW_WINDOW_NAME[] = "Live Preview";
constexpr char HISTOGRAM_WINDOW_NAME[] = "Histogram";

// How often the histogram should be updated in seconds
// Calculating the histogram is a non-trivial computational load
constexpr double HISTOGRAM_UPDATE_PERIOD_S = 0.25;


void make_histogram(cv::Mat &src)
{
    using namespace cv;

    // Quantize to 256 levels
    int histSize[] = {256};

    // Pixel values range from 0 to 255
    float pranges[] = { 0, 256 };
    const float* ranges[] = { pranges };
    MatND hist;

    // compute the histogram from the 0-th channel
    int channels[] = {0};
    calcHist(&src, 1, channels, Mat(), hist, 1, histSize, ranges, true, false);

    // plot histogram on logarithmic y-axis
    double maxVal = log10(Frame::WIDTH * Frame::HEIGHT);
    int scale = 2;
    int height = 256;
    Mat histImg = Mat::zeros(height, 256*scale, CV_8UC3);
    for (int i = 0; i < 256; i++)
    {
        float binVal = hist.at<float>(i);
        float logBinVal = (binVal > 0) ? log10(binVal) : 0.0;
        rectangle(
            histImg,
            Point(i*scale, (int)(height * (1.0 - logBinVal / maxVal))),
            Point( (i+1)*scale - 1, height-1),
            Scalar::all(255),
            -1
        );
    }
    imshow(HISTOGRAM_WINDOW_NAME, histImg);
}


void gain_trackbar_callback(int pos, void *userdata)
{
    gain_trackbar_pos = std::clamp(pos, camera::GAIN_MIN, camera::GAIN_MAX);

    // Gain under manual control
    if (!agc_enabled)
    {
        camera_gain = gain_trackbar_pos;
    }
}

void exposure_trackbar_callback(int pos, void *userdata)
{
    exposure_trackbar_pos = std::clamp(pos, camera::EXPOSURE_MIN_US, camera::EXPOSURE_MAX_US);

    // Exposure time under manual control
    if (!agc_enabled)
    {
        camera_exposure_us = exposure_trackbar_pos;
    }
}

void agc_mode_trackbar_callback(int pos, void *userdata)
{
    // AGC is being disabled so update gain and exposure time to trackbar positions
    if (agc_enabled == true && pos == 1)
    {
        camera_gain = gain_trackbar_pos;
        camera_exposure_us = exposure_trackbar_pos;
    }
    agc_enabled = (pos == 1) ? true : false;
}


void preview(bool color)
{
    printf("preview thread id: %ld\n", syscall(SYS_gettid));

    cv::namedWindow(PREVIEW_WINDOW_NAME, cv::WINDOW_NORMAL);
    cv::resizeWindow(PREVIEW_WINDOW_NAME, 640, 480);
    cv::namedWindow(HISTOGRAM_WINDOW_NAME, 1);

    cv::createTrackbar(
        "agc mode",
        HISTOGRAM_WINDOW_NAME,
        nullptr,
        1,
        agc_mode_trackbar_callback,
        nullptr
    );

    gain_trackbar_pos = camera_gain;
    cv::createTrackbar(
        "gain",
        HISTOGRAM_WINDOW_NAME,
        &gain_trackbar_pos,
        camera::GAIN_MAX,
        gain_trackbar_callback,
        nullptr
    );

    exposure_trackbar_pos = camera_exposure_us;
    cv::createTrackbar(
        "exposure time [us]",
        HISTOGRAM_WINDOW_NAME,
        &exposure_trackbar_pos,
        camera::EXPOSURE_MAX_US,
        exposure_trackbar_callback,
        nullptr
    );

    // deque of 10 timestamps for use in calculating frame rate
    constexpr int NUM_FRAMERATE_FRAMES = 10;
    std::deque<steady_clock::time_point> timestamps(NUM_FRAMERATE_FRAMES, steady_clock::now());

    auto last_histogram_update = steady_clock::now();

    while (!end_program)
    {
        // Get frame from deque
        std::unique_lock<std::mutex> to_preview_deque_lock(to_preview_deque_mutex);
        to_preview_deque_cv.wait(
            to_preview_deque_lock,
            [&]{return !to_preview_deque.empty() || end_program;}
        );
        if (end_program)
        {
            break;
        }
        while (to_preview_deque.size() > 1)
        {
            // Discard all but most recent frame
            to_preview_deque.back()->decrRefCount();
            to_preview_deque.pop_back();
        }
        Frame *frame = to_preview_deque.back();
        to_preview_deque.pop_back();
        to_preview_deque_lock.unlock();

        // Calculate framerate over last NUM_FRAMERATE_FRAMES
        auto now = steady_clock::now();
        timestamps.push_front(now);
        auto then = timestamps.back();
        timestamps.pop_back();
        duration<double> elapsed = now - then;
        double framerate = (double)NUM_FRAMERATE_FRAMES / elapsed.count();
        char window_title[512];
        if (disk_file_exists)
        {
            sprintf(window_title, "%s %.1f FPS %s", PREVIEW_WINDOW_NAME, framerate, (disk_write_enabled) ? ("writing to disk") : ("disk write paused"));
        }
        else
        {
            sprintf(window_title, "%s %.1f FPS", PREVIEW_WINDOW_NAME, framerate);
        }
        cv::setWindowTitle(PREVIEW_WINDOW_NAME, window_title);

        try
        {
            // This throws an exception if the window is closed
            cv::getWindowProperty(PREVIEW_WINDOW_NAME, 0);
        }
        catch (cv::Exception &e)
        {
            // window was closed by user
            frame->decrRefCount();
            break;
        }

        cv::Mat img_raw(Frame::HEIGHT, Frame::WIDTH, CV_8UC1, (void *)(frame->frame_buffer_));

        // Debayer if color camera
        cv::Mat img_preview;
        if (color)
        {
            cv::cvtColor(img_raw, img_preview, cv::COLOR_BayerBG2BGR);
        }
        else
        {
            // Must make a copy so that crosshairs added later do not modify the original frame.
            // Modifications to the original frame could end up being written to disk.
            img_preview = img_raw.clone();
        }

        // Add grey crosshairs
        cv::line(
            img_preview,
            cv::Point(Frame::WIDTH / 2, 0),
            cv::Point(Frame::WIDTH / 2, Frame::HEIGHT - 1),
            cv::Scalar(50, 50, 50),
            1
        );
        cv::line(
            img_preview,
            cv::Point(0, Frame::HEIGHT / 2),
            cv::Point(Frame::WIDTH - 1, Frame::HEIGHT / 2),
            cv::Scalar(50, 50, 50),
            1
        );

        // Show image with crosshairs in a window
        cv::imshow(PREVIEW_WINDOW_NAME, img_preview);


        // Display histogram
        now = steady_clock::now();
        elapsed = now - last_histogram_update;
        if (elapsed.count() >= HISTOGRAM_UPDATE_PERIOD_S)
        {
            make_histogram(img_raw);
            last_histogram_update = now;
        }

        if (agc_enabled)
        {
            cv::setTrackbarPos("exposure time", HISTOGRAM_WINDOW_NAME, camera_exposure_us);
            cv::setTrackbarPos("gain", HISTOGRAM_WINDOW_NAME, camera_gain);
        }

        char key = (char)cv::waitKey(1);

        if (key == 's')
        {
            disk_write_enabled = !disk_write_enabled;
            if (disk_write_enabled)
            {
                printf("Writing frames to disk if filename provided (press s to stop)\n");
            }
            else
            {
                printf("Not writing frames to disk (press s to start)\n");
            }
        }

        frame->decrRefCount();
    }

    printf("Preview thread ending.\n");
}
