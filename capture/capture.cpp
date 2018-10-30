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
atomic_bool gain_updated = true; // true so camera gain is set as soon as possible after startup

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
    static uint32_t hist[256];

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
        uint8_t max_val = 0;
        for (size_t i = 0; i < Frame::IMAGE_SIZE_BYTES; i++)
        {
            uint8_t pixel_val = frame->frame_buffer_[i];
            max_val = std::max(max_val, pixel_val);
            hist[pixel_val]++;
        }
        frame->decrRefCount();

        // Adjust gain
        int old_gain = gain;
        int new_gain = gain;
        if (max_val == 255)
        {
            new_gain--;
        }
        else if (max_val < 240)
        {
            new_gain++;
        }
        new_gain = clamp(new_gain, 0, 510);

        if (new_gain != old_gain)
        {
            gain = new_gain;
            gain_updated = true;
            printf("gain: %d %c\n", new_gain, (new_gain > old_gain ? '+' : '-'));
        }
        else
        {
            printf("gain: %d\n", new_gain);
        }
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
        errx(1, "OpenCamera error: %d", (int)asi_rtn);
    }

    asi_rtn = ASIInitCamera(CamInfo.CameraID);
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "InitCamera error: %d", (int)asi_rtn);
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
        errx(1, "SetROIFormat error: %d", (int)asi_rtn);
    }

    /*
     * Exposure time initialized to 16.667 ms. Will be adjusted dynamically with the custom AGC
     * loop in this capture software. The built-in ZWO auto exposure feature is disabled.
     */
    asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_EXPOSURE, 16667, ASI_FALSE);
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "SetControlValue error for ASI_EXPOSURE: %d", (int)asi_rtn);
    }

    /*
     * Experimentation has shown that the highest value for the BANDWIDTHOVERLOAD parameter that
     * results in stable performance is 94 for the ASI178MC camera and the PC hardware / OS in use.
     * Higher values result in excessive dropped frames.
     */
    asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_BANDWIDTHOVERLOAD, 94, ASI_FALSE);
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "SetControlValue error for ASI_BANDWIDTHOVERLOAD: %d", (int)asi_rtn);
    }

    asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_HIGH_SPEED_MODE, 1, ASI_FALSE);
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "SetControlValue error for ASI_HIGH_SPEED_MODE: %d", (int)asi_rtn);
    }

    asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_WB_B, 90, ASI_FALSE);
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "SetControlValue error for ASI_WB_B: %d", (int)asi_rtn);
    }

    asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_WB_R, 48, ASI_FALSE);
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "SetControlValue error for ASI_WB_R: %d", (int)asi_rtn);
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
        errx(1, "StartVideoCapture error: %d", (int)asi_rtn);
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
            warnx("Frame pool exhausted. :( Frames will likely be dropped.\n");
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
                warnx("SetControlValue error for ASI_GAIN: %d", (int)asi_rtn);
            }

            gain_updated = false;
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
            warnx("GetVideoData failed with error code %d", (int)asi_rtn);
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