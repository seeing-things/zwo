#include <cstdio>
#include <cstring>
#include <cstdint>
#include <csignal>
#include <deque>
#include <thread>
#include <pthread.h>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <algorithm>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <err.h>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include "ASICamera2.h"


// Defined by libASICamera2, returns a tick in milliseconds.
extern unsigned long GetTickCount();

using namespace std;

// All threads should end gracefully when this is true
atomic_bool end_program = false;

// AGC outputs
atomic_int gain = 0;
atomic_bool gain_updated = false;
atomic_int exposure_us = 0;
atomic_bool exposure_updated = false;

// std::deque is not thread safe
mutex to_disk_deque_mutex;
mutex to_preview_deque_mutex;
mutex to_agc_deque_mutex;
mutex unused_deque_mutex;

condition_variable to_disk_deque_cv;
condition_variable to_preview_deque_cv;
condition_variable to_agc_deque_cv;
condition_variable unused_deque_cv;

class Frame;

// FIFOs holding pointers to frame objects
deque<Frame *> to_disk_deque;
deque<Frame *> to_preview_deque;
deque<Frame *> to_agc_deque;
deque<Frame *> unused_deque;


static void set_thread_priority(pthread_t thread, int policy, int priority)
{
    sched_param sch_params;
    sch_params.sched_priority = priority;
    if (pthread_setschedparam(thread, policy, &sch_params))
    {
        err(1, "Failed to set thread priority");
    }
}


static const char *asi_error_str(ASI_ERROR_CODE code)
{
    switch (code)
    {
#define _ASI_ERROR_STR(_code) case _code: return #_code
    _ASI_ERROR_STR(ASI_SUCCESS);
    _ASI_ERROR_STR(ASI_ERROR_INVALID_INDEX);
    _ASI_ERROR_STR(ASI_ERROR_INVALID_ID);
    _ASI_ERROR_STR(ASI_ERROR_INVALID_CONTROL_TYPE);
    _ASI_ERROR_STR(ASI_ERROR_CAMERA_CLOSED);
    _ASI_ERROR_STR(ASI_ERROR_CAMERA_REMOVED);
    _ASI_ERROR_STR(ASI_ERROR_INVALID_PATH);
    _ASI_ERROR_STR(ASI_ERROR_INVALID_FILEFORMAT);
    _ASI_ERROR_STR(ASI_ERROR_INVALID_SIZE);
    _ASI_ERROR_STR(ASI_ERROR_INVALID_IMGTYPE);
    _ASI_ERROR_STR(ASI_ERROR_OUTOF_BOUNDARY);
    _ASI_ERROR_STR(ASI_ERROR_TIMEOUT);
    _ASI_ERROR_STR(ASI_ERROR_INVALID_SEQUENCE);
    _ASI_ERROR_STR(ASI_ERROR_BUFFER_TOO_SMALL);
    _ASI_ERROR_STR(ASI_ERROR_VIDEO_MODE_ACTIVE);
    _ASI_ERROR_STR(ASI_ERROR_EXPOSURE_IN_PROGRESS);
    _ASI_ERROR_STR(ASI_ERROR_GENERAL_ERROR);
    _ASI_ERROR_STR(ASI_ERROR_INVALID_MODE);
#undef _ASI_ERROR_STR

    case ASI_ERROR_END: break;
    }

    static thread_local char buf[128];
    snprintf(buf, sizeof(buf), "(ASI_ERROR_CODE)%d", (int)code);
    return buf;
}


class Frame
{
public:
    Frame() :
        ref_count_(0)
    {
        if (IMAGE_SIZE_BYTES == 0)
        {
            errx(1, "Frame: IMAGE_SIZE_BYTES must be set to a non-zero value before construction");
        }
        frame_buffer_ = new uint8_t[IMAGE_SIZE_BYTES];
        unique_lock<mutex> unused_deque_lock(unused_deque_mutex);
        unused_deque.push_front(this);
        unused_deque_lock.unlock();
        unused_deque_cv.notify_one();
    }

    // Explicit: no copy or move construction or assignment
    Frame(const Frame&)            = delete;
    Frame(Frame&&)                 = delete;
    Frame& operator=(const Frame&) = delete;
    Frame& operator=(Frame&&)      = delete;

    ~Frame()
    {
        delete [] frame_buffer_;
    }

    void incrRefCount()
    {
        // Assume this Frame object has already been removed from the unused frame deque
        ref_count_++;
    }

    void decrRefCount()
    {
        // Ensure this function is reentrant
        std::lock_guard<std::mutex> lock(decr_mutex_);

        if (ref_count_ <= 0)
        {
            errx(1, "Frame.decrRefCount called on Frame where ref_count_ was already zero!");
        }

        ref_count_--;

        if (ref_count_ == 0)
        {
            unique_lock<mutex> unused_deque_lock(unused_deque_mutex);
            unused_deque.push_front(this);
            unused_deque_lock.unlock();
            unused_deque_cv.notify_one();
        }
    }

    // Must be initialized before first object is constructed
    static size_t IMAGE_SIZE_BYTES;

    // Raw image data from camera
    const uint8_t *frame_buffer_;

private:
    std::atomic_int ref_count_;
    std::mutex decr_mutex_;
};
size_t Frame::IMAGE_SIZE_BYTES = 0;


enum SERColorID_t : int32_t
{
    MONO = 0,
    BAYER_RGGB = 8,
    BAYER_GRBG = 9,
    BAYER_GBRG = 10,
    BAYER_BGGR = 11,
    BAYER_CYYM = 16,
    BAYER_YCMY = 17,
    BAYER_YMCY = 18,
    BAYER_MYYC = 19,
    RGB = 100,
    BGR = 101
};


struct __attribute__ ((packed)) SERHeader_t
{
    // 1. This is a historical artifact of the SER format.
    const char FileID[14] = {'L', 'U', 'C', 'A', 'M', '-', 'R', 'E', 'C', 'O', 'R', 'D', 'E', 'R'};

    // 2. Unused field.
    const int32_t LuID = 0;

    // 3. Identifies how color information is encoded.
    SERColorID_t ColorID = BAYER_RGGB;

    // 4. Set to 1 if 16-bit image data is little-endian. 0 for big-endian.
    int32_t LittleEndian = 0;

    // 5. Width of every image in pixels.
    int32_t ImageWidth = 0;

    // 6. Height of every image in pixels.
    int32_t ImageHeight = 0;

    // 7. Number of bits per pixel per color plane (1-16).
    int32_t PixelDepthPerPlane = 8;

    // 8. Number of image frames in SER file.
    int32_t FrameCount = 0;

    // 9. Name of observer. 40 ASCII characters {32-126 dec.}, fill unused characters with 0 dec.
    char Observer[40] = "";

    // 10. Name of used camera. 40 ASCII characters {32-126 dec.}, fill unused characters with 0 dec.
    char Instrument[40] = "";

    // 11. Name of used telescope. 40 ASCII characters {32-126 dec.}, fill unused characters with 0 dec.
    char Telescope[40] = "";

    // 12. Start time of image stream (local time). Must be >= 0.
    int64_t DateTime = 0;

    // 13. Start time of image stream in UTC.
    int64_t DateTime_UTC = 0;
};


class SERFile
{
public:
    SERFile(
        const char *filename,
        int32_t width,
        int32_t height,
        SERColorID_t color_id = BAYER_RGGB,
        int32_t bit_depth = 8,
        const char *observer = "",
        const char *instrument = "",
        const char *telescope = "",
        bool add_trailer = true
    ) :
        UTC_OFFSET_S(utcOffset()),
        add_trailer_(add_trailer)
    {
        bytes_per_frame_ = width * height * ((bit_depth - 1) / 8 + 1);
        if (color_id == RGB || color_id == BGR)
        {
            bytes_per_frame_ *= 3;
        }

        fd_ = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd_ < 0)
        {
            err(1, "open(%s) failed", filename);
        }

        // Extend file size to length of header
        if (ftruncate(fd_, sizeof(SERHeader_t)))
        {
            err(1, "could not extend file to make room for SER header");
        }

        // Reposition file descriptor offset past header
        if (lseek(fd_, 0, SEEK_END) < 0)
        {
            err(1, "could not seek past header in SER file");
        }

        // Map the header portion of the file into memory
        header_ = (SERHeader_t *)mmap(
            0,
            sizeof(SERHeader_t),
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            fd_,
            0
        );
        if (header_ == MAP_FAILED)
        {
            err(1, "mmap for SERFile header failed");
        }

        // Use placement new operator to init header to defaults in struct definition
        new(header_) SERHeader_t;

        // Init header values based on constructor args
        header_->ImageWidth = width;
        header_->ImageHeight = height;
        header_->ColorID = color_id;
        header_->PixelDepthPerPlane = bit_depth;
        strncpy(header_->Observer, observer, 40);
        strncpy(header_->Instrument, instrument, 40);
        strncpy(header_->Telescope, telescope, 40);
        makeTimestamps(&(header_->DateTime_UTC), &(header_->DateTime));
    }

    ~SERFile()
    {
        if (add_trailer_)
        {
            if (header_->FrameCount != static_cast<int32_t>(frame_timestamps_.size()))
            {
                warnx("SERFile class frame count %d does not match timestamp vector size %zu",
                    header_->FrameCount,
                    frame_timestamps_.size()
                );
            }

            size_t trailer_len_bytes = sizeof(int64_t) * frame_timestamps_.size();
            ssize_t n = write(fd_, frame_timestamps_.data(), trailer_len_bytes);
            if (n < 0)
            {
                err(1, "write failed");
            }
            else if (n != static_cast<ssize_t>(trailer_len_bytes))
            {
                err(1, "write incomplete (%zd/%zu)", n, trailer_len_bytes);
            }
        }

        if (munmap(header_, sizeof(SERHeader_t)))
        {
            err(1, "munmap for SERFile header failed");
        }
        (void)close(fd_);
    }

    void addFrame(Frame &frame)
    {
        if (bytes_per_frame_ != Frame::IMAGE_SIZE_BYTES)
        {
            errx(
                1,
                "frame size %zu bytes does not match expected size %zu bytes",
                Frame::IMAGE_SIZE_BYTES,
                bytes_per_frame_
            );
        }

        if (add_trailer_)
        {
            int64_t utc_timestamp;
            makeTimestamps(&utc_timestamp, nullptr);
            frame_timestamps_.push_back(utc_timestamp);
        }

        ssize_t n = write(fd_, frame.frame_buffer_, bytes_per_frame_);
        if (n < 0)
        {
            err(1, "write failed");
        }
        else if (n != static_cast<ssize_t>(bytes_per_frame_))
        {
            err(1, "write incomplete (%zd/%zu)", n, bytes_per_frame_);
        }

        header_->FrameCount++;
    }

    static int64_t utcOffset()
    {
        /*
         * Returns the UTC offset in seconds. This requires ugly methods because the standard
         * libraries as of 2018 do not provide a straightforward way to get this information.
         */
        time_t t = time(nullptr);
        tm *local_time = localtime(&t);
        char tz_str[16];
        strftime(tz_str, 16, "%z", local_time);
        int hours;
        int minutes;
        sscanf(tz_str, "%3d%2d", &hours, &minutes);
        return hours * 3600 + minutes;
    }


private:
    const int64_t UTC_OFFSET_S;
    SERHeader_t *header_;
    int fd_;
    size_t bytes_per_frame_;
    bool add_trailer_;
    vector<int64_t> frame_timestamps_;

    void makeTimestamps(int64_t *utc, int64_t *local)
    {
        using namespace std::chrono;

        /*
         * Number of ticks from the Visual Basic Date data type to the Unix time epoch. The
         * VB Date type is the number of "ticks" since Jan 1, year 0001 in the Gregorian calendar,
         * where each tick is 100 ns.
         */
        constexpr int64_t VB_DATE_TICKS_TO_UNIX_EPOCH = 621'355'968'000'000'000LL;
        constexpr int64_t VB_DATE_TICKS_PER_SEC = 10'000'000LL;

        system_clock::time_point now = system_clock::now();
        int64_t ns_since_epoch = duration_cast<nanoseconds>(now.time_since_epoch()).count();

        int64_t utc_tick = (ns_since_epoch / 100) + VB_DATE_TICKS_TO_UNIX_EPOCH;
        if (utc != nullptr)
        {
            *utc = utc_tick;
        }

        if (local != nullptr)
        {
            *local = utc_tick + UTC_OFFSET_S * VB_DATE_TICKS_PER_SEC;
        }
    }
};


void sigint_handler(int signal)
{
    end_program = true;
    to_disk_deque_cv.notify_one();
    to_preview_deque_cv.notify_one();
    to_agc_deque_cv.notify_one();
    unused_deque_cv.notify_one();
}


// Writes frames of data to disk as quickly as possible. Run as a thread.
void write_to_disk(
    const char *filename,
    int32_t image_width,
    int32_t image_height)
{
    printf("disk thread id: %ld\n", syscall(SYS_gettid));

    SERFile ser_file(filename, image_width, image_height, BAYER_RGGB, 8, "", "ZWO ASI178MC", "");

    while (!end_program)
    {
        // Get next frame from deque
        unique_lock<mutex> to_disk_deque_lock(to_disk_deque_mutex);
        to_disk_deque_cv.wait(to_disk_deque_lock, [&]{return !to_disk_deque.empty() || end_program;});
        if (end_program)
        {
            break;
        }
        Frame *frame = to_disk_deque.back();
        to_disk_deque.pop_back();
        to_disk_deque_lock.unlock();

        ser_file.addFrame(*frame);

        frame->decrRefCount();
    }

    printf("Disk thread ending.\n");
}


void preview()
{
    constexpr char WINDOW_NAME[] = "Live Preview";

    printf("preview thread id: %ld\n", syscall(SYS_gettid));

    cv::namedWindow(WINDOW_NAME, cv::WINDOW_NORMAL);
    cv::resizeWindow(WINDOW_NAME, 640, 480);

    while (!end_program)
    {
        // Get frame from deque
        unique_lock<mutex> to_preview_deque_lock(to_preview_deque_mutex);
        to_preview_deque_cv.wait(to_preview_deque_lock, [&]{return !to_preview_deque.empty() || end_program;});
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
        cv::Mat img_bgr;
        cv::cvtColor(img_bayer_bg, img_bgr, CV_BayerBG2BGR);
        cv::imshow(WINDOW_NAME, img_bgr);
        cv::waitKey(1);

        frame->decrRefCount();
    }

    printf("Preview thread ending.\n");
}


void agc()
{
    constexpr int GAIN_MIN = 0;
    constexpr int GAIN_MAX = 510; // 510 is the maximum value for this camera
    constexpr int EXPOSURE_MIN_US = 32; // 32 is the minimum value for this camera
    constexpr int EXPOSURE_MAX_US = 16'667; // Max for ~60 FPS

    static uint32_t hist[256];

    /*
     * The AGC directly servos this variable which has range [0.0, 1.0]. The camera gain and
     * exposure time are both functions of this value.
     */
    static double agc_value = 0.0;

    printf("gain thread id: %ld\n", syscall(SYS_gettid));

    while (!end_program)
    {
        // Get frame from deque
        unique_lock<mutex> to_agc_deque_lock(to_agc_deque_mutex);
        to_agc_deque_cv.wait(to_agc_deque_lock, [&]{return !to_agc_deque.empty() || end_program;});
        if (end_program)
        {
            break;
        }
        while (to_agc_deque.size() > 1)
        {
            // Discard all but most recent frame
            to_agc_deque.back()->decrRefCount();
            to_agc_deque.pop_back();
        }
        Frame *frame = to_agc_deque.back();
        to_agc_deque.pop_back();
        to_agc_deque_lock.unlock();

        // Clear histogram array
        memset(hist, 0, sizeof(hist));

        // Generate histogram
        for (size_t i = 0; i < Frame::IMAGE_SIZE_BYTES; i++)
        {
            uint8_t pixel_val = frame->frame_buffer_[i];
            hist[pixel_val]++;
        }
        frame->decrRefCount();

        // Calculate Nth percentile pixel value
        constexpr float percentile = 0.999;
        uint32_t integral_threshold = (uint32_t)((1.0 - percentile) * Frame::IMAGE_SIZE_BYTES);
        uint8_t upper_tail_val = 255;
        uint32_t integral = hist[upper_tail_val];;
        while (integral < integral_threshold)
        {
            upper_tail_val--;
            integral += hist[upper_tail_val];
        }

        // Adjust AGC
        if (upper_tail_val >= 250)
        {
            agc_value -= 0.01;
        }
        else if (upper_tail_val < 230)
        {
            agc_value += 0.01;
        }
        agc_value = clamp(agc_value, 0.0, 1.0);

        printf("AGC value: %.3f, upper tail value: %03d", agc_value, upper_tail_val);

        // derive new camera gain
        int new_gain = clamp((int)(4.0 * GAIN_MAX * agc_value - (3.0 * GAIN_MAX)), GAIN_MIN, GAIN_MAX);
        printf(", gain: %03d", new_gain);
        if (new_gain != gain)
        {
            printf(" %c", (new_gain > gain ? '+' : '-'));
            gain = new_gain;
            gain_updated = true;
        }

        // derive new camera exposure time
        int new_exposure_us = clamp(
            (int)(4.0 / 3.0 * EXPOSURE_MAX_US * agc_value),
            EXPOSURE_MIN_US,
            EXPOSURE_MAX_US
        );
        printf(", exposure: %05.2f ms", (float)new_exposure_us / 1.0e3);
        if (new_exposure_us != exposure_us)
        {
            printf(" %c", (new_exposure_us > exposure_us ? '+' : '-'));
            exposure_us = new_exposure_us;
            exposure_updated = true;
        }

        printf("\n");
    }

    printf("AGC thread ending.\n");
}


int main(int argc, char *argv[])
{
    ASI_ERROR_CODE asi_rtn;
    ASI_CAMERA_INFO CamInfo;

    constexpr int AGC_PERIOD_MS = 100;

    signal(SIGINT, sigint_handler);

    if (argc < 2)
    {
        errx(1, "Usage: %s <output_filename>", argv[0]);
    }

    printf("main thread id: %ld\n", syscall(SYS_gettid));

    /*
     * Set real-time priority for the main thread. All threads created later, including by the ASI
     * library, will inherit this priority.
     */
    set_thread_priority(pthread_self(), SCHED_RR, 10);

    int numDevices = ASIGetNumOfConnectedCameras();
    if (numDevices <= 0)
    {
        errx(1, "No cameras connected.");
    }
    else
    {
        printf("Found %d cameras, first of these selected.\n", numDevices);
    }

    // Select the first camera
    int cam_index = 0;
    ASIGetCameraProperty(&CamInfo, cam_index);

    asi_rtn = ASIOpenCamera(CamInfo.CameraID);
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "OpenCamera error: %s", asi_error_str(asi_rtn));
    }

    asi_rtn = ASIInitCamera(CamInfo.CameraID);
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "InitCamera error: %s", asi_error_str(asi_rtn));
    }

    asi_rtn = ASISetROIFormat(
        CamInfo.CameraID,
        CamInfo.MaxWidth,
        CamInfo.MaxHeight,
        1,
        ASI_IMG_RAW8
    );
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "SetROIFormat error: %s", asi_error_str(asi_rtn));
    }

    /*
     * Experimentation has shown that the highest value for the BANDWIDTHOVERLOAD parameter that
     * results in stable performance is 94 for the ASI178MC camera and the PC hardware / OS in use.
     * Higher values result in excessive dropped frames.
     */
    asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_BANDWIDTHOVERLOAD, 94, ASI_FALSE);
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "SetControlValue error for ASI_BANDWIDTHOVERLOAD: %s", asi_error_str(asi_rtn));
    }

    asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_HIGH_SPEED_MODE, 1, ASI_FALSE);
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "SetControlValue error for ASI_HIGH_SPEED_MODE: %s", asi_error_str(asi_rtn));
    }

    asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_WB_B, 90, ASI_FALSE);
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "SetControlValue error for ASI_WB_B: %s", asi_error_str(asi_rtn));
    }

    asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_WB_R, 48, ASI_FALSE);
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "SetControlValue error for ASI_WB_R: %s", asi_error_str(asi_rtn));
    }

    // Create pool of frame buffers
    Frame::IMAGE_SIZE_BYTES = CamInfo.MaxWidth * CamInfo.MaxHeight;
    constexpr size_t FRAME_POOL_SIZE = 64;
    static deque<Frame> frames;
    for(size_t i = 0; i < FRAME_POOL_SIZE; i++)
    {
        // Frame objects add themselves to unused_deque on construction
        frames.emplace_back();
    }

    // Start threads
    static thread write_to_disk_thread(
        write_to_disk,
        argv[1],
        CamInfo.MaxWidth,
        CamInfo.MaxHeight
    );
    static thread preview_thread(preview);
    static thread agc_thread(agc);

    // These threads do not need real-time priority
    set_thread_priority(preview_thread.native_handle(), SCHED_OTHER, 0);
    set_thread_priority(agc_thread.native_handle(), SCHED_OTHER, 0);


    asi_rtn = ASIStartVideoCapture(CamInfo.CameraID);
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "StartVideoCapture error: %s", asi_error_str(asi_rtn));
    }

    int frame_count = 0;
    int time1 = GetTickCount();
    int agc_last_dispatch_ts = GetTickCount();
    while (!end_program)
    {
        // Get pointer to an available Frame object
        unique_lock<mutex> unused_deque_lock(unused_deque_mutex);
        while (unused_deque.empty() && !end_program)
        {
            warnx("Frame pool exhausted. :( Frames will likely be dropped.");
            unused_deque_cv.wait(unused_deque_lock);
        }
        if (end_program)
        {
            break;
        }
        Frame *frame = unused_deque.back();
        unused_deque.pop_back();
        unused_deque_lock.unlock();

        // Matching decrement in write_to_disk thread (except in case of failure to get frame data)
        frame->incrRefCount();

        // Set camera gain if value was updated by AGC thread
        if (gain_updated)
        {
            asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_GAIN, gain, ASI_FALSE);
            if (asi_rtn != ASI_SUCCESS)
            {
                warnx("SetControlValue error for ASI_GAIN: %s", asi_error_str(asi_rtn));
            }

            gain_updated = false;
        }

        // Set exposure time if value was updated by AGC thread
        if (exposure_updated)
        {
            asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_EXPOSURE, exposure_us, ASI_FALSE);
            if (asi_rtn != ASI_SUCCESS)
            {
                errx(1, "SetControlValue error for ASI_EXPOSURE: %s", asi_error_str(asi_rtn));
            }
        }

        // Populate frame buffer with data from camera
        asi_rtn = ASIGetVideoData(
            CamInfo.CameraID,
            const_cast<uint8_t *>(frame->frame_buffer_),
            Frame::IMAGE_SIZE_BYTES,
            200
        );
        if (asi_rtn == ASI_SUCCESS)
        {
            frame_count++;

            // Dispatch a subset of frames to AGC thread
            int now_ts = GetTickCount();
            if (now_ts - agc_last_dispatch_ts > AGC_PERIOD_MS)
            {
                agc_last_dispatch_ts = now_ts;

                // Put this frame in the deque headed for AGC thread
                frame->incrRefCount();
                unique_lock<mutex> to_agc_deque_lock(to_agc_deque_mutex);
                to_agc_deque.push_front(frame);
                to_agc_deque_lock.unlock();
                to_agc_deque_cv.notify_one();
            }

            // Put this frame in the deque headed for live preview thread if the deque is empty
            if (to_preview_deque.empty())
            {
                frame->incrRefCount();
                unique_lock<mutex> to_preview_deque_lock(to_preview_deque_mutex);
                to_preview_deque.push_front(frame);
                to_preview_deque_lock.unlock();
                to_preview_deque_cv.notify_one();
            }

            // Put this frame in the deque headed for write to disk thread
            unique_lock<mutex> to_disk_deque_lock(to_disk_deque_mutex);
            to_disk_deque.push_front(frame);
            to_disk_deque_lock.unlock();
            to_disk_deque_cv.notify_one();
        }
        else
        {
            warnx("GetVideoData failed with error code %s", asi_error_str(asi_rtn));
            frame->decrRefCount();
        }

        int time2 = GetTickCount();

        if (time2 - time1 > 1000)
        {
            int num_dropped_frames;
            ASIGetDroppedFrames(CamInfo.CameraID, &num_dropped_frames);
            printf("Frame count: %06d, Dropped frames: %06d\n", frame_count, num_dropped_frames);
            printf(
                "To-disk queue: %zu frames, to-AGC queue: %zu frames, pool: %zu free frames.\n",
                to_disk_deque.size(),
                to_agc_deque.size(),
                unused_deque.size()
            );
            time1 = GetTickCount();
        }
    }

    printf("Main thread cleaning up.\n");

    ASIStopVideoCapture(CamInfo.CameraID);
    ASICloseCamera(CamInfo.CameraID);

    write_to_disk_thread.join();
    preview_thread.join();
    agc_thread.join();

    printf("Main thread ending.\n");

    return 0;
}