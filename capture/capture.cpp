#include <cstdio>
#include <cstring>
#include <cstdint>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include "ASICamera2.h"

// Defined by libASICamera2, returns a tick in milliseconds.
extern unsigned long GetTickCount();

using namespace std;

// std::deque is not thread safe
mutex frame_deque_mutex;
mutex unused_frame_deque_mutex;

// ASICamera2 API is not known to be thread-safe
mutex asi_mutex;


class Frame
{
public:
    Frame(size_t image_size_bytes, deque<Frame *> *unused_frame_deque) :
        image_size_bytes_(image_size_bytes),
        unused_frame_deque_(unused_frame_deque),
        ref_count_(0)
    {
        frame_buffer_ = new uint8_t[image_size_bytes];
        unused_frame_deque_->push_front(this);
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
            unique_lock<mutex> unused_frame_deque_lock(unused_frame_deque_mutex);
            unused_frame_deque_->push_front(this);
            unused_frame_deque_lock.unlock();
        }
    }

    const size_t image_size_bytes_;
    // TODO: this ptr should probably be const to guarantee that threads can't step on each other
    // WRT the data; use a const_cast in a method or something for the data initialization
    uint8_t *frame_buffer_;

private:
    deque<Frame *> * const unused_frame_deque_;
    std::atomic_int ref_count_;
    std::mutex decr_mutex_;
};


// Writes frames of data to disk as quickly as possible. Run as a thread.
void write_to_disk(
    int fd,
    deque<Frame *> &frame_deque)
{
    // TODO: this should really use a condition variable and wait on that, instead of polling every
    // 1 millisecond, so that it can write out the frame IMMEDIATELY, as soon as possible
    while (true)
    {
        unique_lock<mutex> frame_deque_lock(frame_deque_mutex);
        if (frame_deque.empty())
        {
            frame_deque_lock.unlock();
            usleep(1000);
        }
        else
        {
            Frame *frame = frame_deque.back();
            frame_deque_lock.unlock();
            ssize_t n = write(fd, frame->frame_buffer_, frame->image_size_bytes_);
            if (n < 0)
            {
                err(1, "write failed");
            }
            else if (n != static_cast<ssize_t>(frame->image_size_bytes_))
            {
                err(1, "write incomplete (%zd/%zu)", n, frame->image_size_bytes_);
            }
            frame_deque_lock.lock();
            frame_deque.pop_back();
            frame_deque_lock.unlock();
            frame->decrRefCount();
        }
    }
}


void agc(
    ASI_CAMERA_INFO CamInfo,
    deque<Frame *> &frame_deque)
{
    static uint32_t hist[256];
    int gain = 0;

    /*
     * Initialize camera gain. Will be adjusted dynamically by this control loop. The ZWO driver's
     * auto gain feature is disabled.
     */
    unique_lock<mutex> asi_lock(asi_mutex);
    ASI_ERROR_CODE asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_GAIN, gain, ASI_FALSE);
    asi_lock.unlock();
    if (asi_rtn != ASI_SUCCESS)
    {
        warnx("SetControlValue error for ASI_GAIN: %d", (int)asi_rtn);
    }

    while (true)
    {
        usleep(100'000);

        unique_lock<mutex> frame_deque_lock(frame_deque_mutex);
        if (frame_deque.empty())
        {
            // no frames available to look at
            frame_deque_lock.unlock();
            printf("AGC: no frames available!\n");
            continue;
        }

        /*
         * Grab pointer to the most recent frame. Increment reference count *before* releasing
         * deque mutex to ensure that the write_to_disk thread can't decrement the reference count
         * before it is incremented here.
         */
        Frame *frame = frame_deque.front();
        frame->incrRefCount();
        frame_deque_lock.unlock();

        // Clear histogram array
        memset(hist, 0, sizeof(hist));

        // Generate histogram
        uint8_t max_val = 0;
        for (size_t i = 0; i < frame->image_size_bytes_; i++)
        {
            uint8_t pixel_val = frame->frame_buffer_[i];
            max_val = std::max(max_val, pixel_val);
            hist[pixel_val]++;
        }
        frame->decrRefCount();

        // Adjust gain
        int old_gain = gain;
        if (max_val == 255)
        {
            gain--;
        }
        else if (max_val < 240)
        {
            gain++;
        }
        gain = clamp(gain, 0, 510);

        if (gain != old_gain)
        {
            printf("gain: %d %c\n", gain, (gain > old_gain ? '+' : '-'));
            asi_lock.lock();
            asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_GAIN, gain, ASI_FALSE);
            asi_lock.unlock();
            if (asi_rtn != ASI_SUCCESS)
            {
                warnx("SetControlValue error for ASI_GAIN: %d", (int)asi_rtn);
            }
        }
        else
        {
            printf("gain: %d\n", gain);
        }
    }
}


int main()
{
    ASI_ERROR_CODE asi_rtn;
    ASI_CAMERA_INFO CamInfo;

    constexpr int width = 3096;
    constexpr int height = 2080;
    constexpr ASI_IMG_TYPE image_type = ASI_IMG_RAW8;

    {
        // Hold the ASI mutex during camera initialization
        lock_guard<mutex> asi_lock(asi_mutex);

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

        asi_rtn = ASISetROIFormat(CamInfo.CameraID, width, height, 1, image_type);
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
    }

    size_t image_size_bytes;
    switch (image_type)
    {
        case ASI_IMG_RAW16:
            image_size_bytes = width * height * 2;
            break;
        case ASI_IMG_RGB24:
            image_size_bytes = width * height * 3;
            break;
        default:
            image_size_bytes = width * height;
    }
    printf("Each frame contains %zu bytes\n", image_size_bytes);

    // FIFOs holding pointers to frame objects
    static deque<Frame *> unused_frame_deque;
    static deque<Frame *> frame_deque;

    // Create pool of frame buffers
    constexpr size_t FRAME_POOL_SIZE = 64;
    static deque<Frame> frames;
    for(size_t i = 0; i < FRAME_POOL_SIZE; i++)
    {
        // Frame objects add themselves to unused_frame_deque on construction
        frames.emplace_back(image_size_bytes, &unused_frame_deque);
    }

    constexpr char FILE_NAME[] = "/home/rgottula/Desktop/test.bin";
    int fd = open(FILE_NAME, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        err(1, "open(%s) failed", FILE_NAME);
    }

    // start thread for writing frame data to disk
    static thread write_to_disk_thread(
        write_to_disk,
        fd,
        ref(frame_deque)
    );

    // start thread for AGC
    static thread agc_thread(
        agc,
        CamInfo,
        ref(frame_deque)
    );

    {
        lock_guard<mutex> asi_lock(asi_mutex);
        asi_rtn = ASIStartVideoCapture(CamInfo.CameraID);
    }
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "StartVideoCapture error: %d", (int)asi_rtn);
    }

    int frame_count = 0;
    int time1 = GetTickCount();
    while (true)
    {
        // Get pointer to an available Frame object
        unique_lock<mutex> unused_frame_deque_lock(unused_frame_deque_mutex);
        if (unused_frame_deque.empty())
        {
            unused_frame_deque_lock.unlock();
            // TODO: use condition variable signalling here too
            usleep(1000);
            continue;
        }
        Frame *frame = unused_frame_deque.back();
        unused_frame_deque.pop_back();
        unused_frame_deque_lock.unlock();

        // Matching decrement in write_to_disk thread
        frame->incrRefCount();

        // Populate frame buffer with data from camera
        {
            lock_guard<mutex> asi_lock(asi_mutex);
            asi_rtn = ASIGetVideoData(
                CamInfo.CameraID,
                frame->frame_buffer_,
                frame->image_size_bytes_,
                200
            );
        }
        if (asi_rtn == ASI_SUCCESS)
        {
            frame_count++;
        }
        else
        {
            warnx("GetVideoData failed with error code %d", (int)asi_rtn);
        }

        // Put this frame in the deque headed for write to disk thread
        unique_lock<mutex> frame_deque_lock(frame_deque_mutex);
        frame_deque.push_front(frame);
        frame_deque_lock.unlock();

        int time2 = GetTickCount();

        if (time2 - time1 > 1000)
        {
            int num_dropped_frames;
            {
                lock_guard<mutex> asi_lock(asi_mutex);
                ASIGetDroppedFrames(CamInfo.CameraID, &num_dropped_frames);
            }
            printf("frame count: %d dropped frames: %d\n", frame_count, num_dropped_frames);
            printf(
                "The queue has %zu frames, the pool has %zu free buffers.\n",
                frame_deque.size(),
                unused_frame_deque.size()
            );
            time1 = GetTickCount();
        }
    }

    // this is currently unreachable but keeping it since it shows the proper way to shut down
    {
        lock_guard<mutex> asi_lock(asi_mutex);
        ASIStopVideoCapture(CamInfo.CameraID);
        ASICloseCamera(CamInfo.CameraID);
    }

    // TODO: implement SIGINT handler maybe
    // TODO: explicitly join() threads on exit (requires signalling them in some manner)

    return 0;
}