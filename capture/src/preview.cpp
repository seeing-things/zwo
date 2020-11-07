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


extern std::atomic_bool end_program;

// AGC enable state
extern std::atomic_bool agc_enabled;

// AGC outputs (possibly under manual control here)
extern std::atomic_int camera_gain;
extern std::atomic_int camera_exposure_us;

extern std::mutex to_preview_deque_mutex;
extern std::condition_variable to_preview_deque_cv;
extern std::deque<Frame *> to_preview_deque;

// trackbar positions
int gain_trackbar_pos;
int exposure_trackbar_pos;


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
    double maxVal = log10(camera::WIDTH * camera::HEIGHT);
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
    imshow("Histogram", histImg);
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


void preview()
{
    constexpr char WINDOW_NAME[] = "Live Preview";

    printf("preview thread id: %ld\n", syscall(SYS_gettid));

    cv::namedWindow(WINDOW_NAME, cv::WINDOW_NORMAL);
    cv::resizeWindow(WINDOW_NAME, 640, 480);
    cv::namedWindow("Histogram", 1);

    cv::createTrackbar(
        "agc mode",
        "Histogram",
        nullptr,
        1,
        agc_mode_trackbar_callback,
        nullptr
    );

    gain_trackbar_pos = camera_gain;
    cv::createTrackbar(
        "gain",
        "Histogram",
        &gain_trackbar_pos,
        camera::GAIN_MAX,
        gain_trackbar_callback,
        nullptr
    );

    exposure_trackbar_pos = camera_exposure_us;
    cv::createTrackbar(
        "exposure time",
        "Histogram",
        &exposure_trackbar_pos,
        camera::EXPOSURE_MAX_US,
        exposure_trackbar_callback,
        nullptr
    );

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

        try
        {
            // This throws an exception if the window is closed
            cv::getWindowProperty(WINDOW_NAME, 0);
        }
        catch (cv::Exception &e)
        {
            // window was closed by user
            frame->decrRefCount();
            break;
        }

        cv::Mat img_bayer_bg(2080, 3096, CV_8UC1, (void *)(frame->frame_buffer_));

        // Debayer
        cv::Mat img_bgr;
        cv::cvtColor(img_bayer_bg, img_bgr, cv::COLOR_BayerBG2BGR);

        // Add grey crosshairs
        cv::line(
            img_bgr,
            cv::Point(camera::WIDTH / 2, 0),
            cv::Point(camera::WIDTH / 2, camera::HEIGHT - 1),
            cv::Scalar(50, 50, 50),
            1
        );
        cv::line(
            img_bgr,
            cv::Point(0, camera::HEIGHT / 2),
            cv::Point(camera::WIDTH - 1, camera::HEIGHT / 2),
            cv::Scalar(50, 50, 50),
            1
        );

        // Show color image with crosshairs in a window
        cv::imshow(WINDOW_NAME, img_bgr);

        // Display histogram
        make_histogram(img_bayer_bg);

        if (agc_enabled)
        {
            cv::setTrackbarPos("exposure time", "Histogram", camera_exposure_us);
            cv::setTrackbarPos("gain", "Histogram", camera_gain);
        }

        cv::waitKey(1);

        frame->decrRefCount();
    }

    printf("Preview thread ending.\n");
}
