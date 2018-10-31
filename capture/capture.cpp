#include <cstdio>
#include <cstring>
#include <cstdint>
#include <csignal>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <algorithm>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <err.h>
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
mutex to_agc_deque_mutex;
mutex unused_deque_mutex;

condition_variable to_disk_deque_cv;
condition_variable to_agc_deque_cv;
condition_variable unused_deque_cv;

class Frame;

// FIFOs holding pointers to frame objects
deque<Frame *> to_disk_deque;
deque<Frame *> to_agc_deque;
deque<Frame *> unused_deque;


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


void sigint_handler(int signal)
{
    end_program = true;
    to_disk_deque_cv.notify_one();
    to_agc_deque_cv.notify_one();
    unused_deque_cv.notify_one();
}


// Writes frames of data to disk as quickly as possible. Run as a thread.
void write_to_disk(int fd)
{
    printf("disk thread id: %ld\n", syscall(SYS_gettid));

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

        // Write frame data to disk
        ssize_t n = write(fd, frame->frame_buffer_, Frame::IMAGE_SIZE_BYTES);
        if (n < 0)
        {
            err(1, "write failed");
        }
        else if (n != static_cast<ssize_t>(Frame::IMAGE_SIZE_BYTES))
        {
            err(1, "write incomplete (%zd/%zu)", n, Frame::IMAGE_SIZE_BYTES);
        }

        frame->decrRefCount();
    }

    (void)close(fd);

    printf("Disk thread ending.\n");
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


int main()
{
    ASI_ERROR_CODE asi_rtn;
    ASI_CAMERA_INFO CamInfo;

    constexpr int AGC_PERIOD_MS = 100;

    signal(SIGINT, sigint_handler);

    printf("main thread id: %ld\n", syscall(SYS_gettid));

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

    constexpr char FILE_NAME[] = "/home/rgottula/Desktop/test.bin";
    int fd = open(FILE_NAME, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        err(1, "open(%s) failed", FILE_NAME);
    }

    // start thread for writing frame data to disk
    static thread write_to_disk_thread(write_to_disk, fd);

    // start thread for AGC
    static thread agc_thread(agc);

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
    agc_thread.join();

    printf("Main thread ending.\n");

    return 0;
}