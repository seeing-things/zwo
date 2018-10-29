#include <cstdio>
#include <cstring>
#include <cstdint>
#include <deque>
#include <thread>
#include <mutex>
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


// Writes frames of data to disk as quickly as possible. Run as a thread.
void write_to_disk(
    int fd,
    size_t image_size_bytes,
    deque<uint8_t *> &frame_deque,
    deque<uint8_t *> &unused_frame_deque)
{
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
            uint8_t *frame_buffer = frame_deque.back();
            frame_deque_lock.unlock();
            ssize_t n = write(fd, frame_buffer, image_size_bytes);
            if (n < 0)
            {
                err(1, "write failed");
            }
            else if (n != image_size_bytes)
            {
                err(1, "write incomplete (%zd/%zu)", n, image_size_bytes);
            }
            frame_deque_lock.lock();
            frame_deque.pop_back();
            frame_deque_lock.unlock();
            unique_lock<mutex> unused_frame_deque_lock(unused_frame_deque_mutex);
            unused_frame_deque.push_front(frame_buffer);
            unused_frame_deque_lock.unlock();
        }
    }
}


void agc(
    ASI_CAMERA_INFO CamInfo,
    size_t image_size_bytes,
    deque<uint8_t *> &frame_deque)
{
    uint32_t hist[256];
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
        printf("SetControlValue error for ASI_GAIN: %d\n", (int)asi_rtn);
    }

    while (true)
    {
        usleep(100000);

        unique_lock<mutex> frame_deque_lock(frame_deque_mutex);
        if (frame_deque.empty())
        {
            // no frames available to look at
            frame_deque_lock.unlock();
            continue;
        }

        // Grab pointer to the most recent frame
        // TODO: Add reference counter to ensure this frame is not overwritten while this thread is
        // using it!
        uint8_t *frame_buffer = frame_deque.front();
        frame_deque_lock.unlock();

        // Clear histogram array
        memset(hist, 0, sizeof(hist));

        // Generate histogram
        uint8_t max_val = 0;
        for (size_t i = 0; i < image_size_bytes; i++)
        {
            uint8_t pixel_val = frame_buffer[i];
            max_val = std::max(max_val, pixel_val);
            hist[pixel_val]++;
        }

        // Adjust gain
        bool gain_changed = true;
        if (max_val == 255)
        {
            gain--;
        }
        else if (max_val < 240)
        {
            gain++;
        }
        else
        {
            gain_changed = false;
        }

        if (gain_changed)
        {
            printf("gain: %d\n", gain);
            asi_lock.lock();
            asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_GAIN, gain, ASI_FALSE);
            asi_lock.unlock();
            if (asi_rtn != ASI_SUCCESS)
            {
                printf("SetControlValue error for ASI_GAIN: %d\n", (int)asi_rtn);
            }
        }
    }
}

int main()
{
    // Hold the ASI mutex during camera initialization
    unique_lock<mutex> asi_lock(asi_mutex);

    int numDevices = ASIGetNumOfConnectedCameras();
    if (numDevices <= 0)
    {
        printf("No cameras connected.\n");
        return 1;
    }
    else
    {
        printf("Found %d cameras, first of these selected.\n", numDevices);
    }

    // Select the first camera
    ASI_CAMERA_INFO CamInfo;
    int cam_index = 0;
    ASIGetCameraProperty(&CamInfo, cam_index);

    ASI_ERROR_CODE asi_rtn = ASIOpenCamera(CamInfo.CameraID);
    if (asi_rtn != ASI_SUCCESS)
    {
        printf("OpenCamera error: %d\n", (int)asi_rtn);
        return 1;
    }

    asi_rtn = ASIInitCamera(CamInfo.CameraID);
    if (asi_rtn != ASI_SUCCESS)
    {
        printf("InitCamera error: %d\n", (int)asi_rtn);
        return 1;
    }

    int width = 3096;
    int height = 2080;
    ASI_IMG_TYPE image_type = ASI_IMG_RAW8;
    asi_rtn = ASISetROIFormat(CamInfo.CameraID, width, height, 1, image_type);
    if (asi_rtn != ASI_SUCCESS)
    {
        printf("SetROIFormat error: %d\n", (int)asi_rtn);
        return 1;
    }

    /*
     * Exposure time initialized to 16.667 ms. Will be adjusted dynamically with the custom AGC
     * loop in this capture software. The built-in ZWO auto exposure feature is disabled.
     */
    asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_EXPOSURE, 16667, ASI_FALSE);
    if (asi_rtn != ASI_SUCCESS)
    {
        printf("SetControlValue error for ASI_EXPOSURE: %d\n", (int)asi_rtn);
        return 1;
    }

    /*
     * Experimentation has shown that the highest value for the BANDWIDTHOVERLOAD parameter that
     * results in stable performance is 94 for the ASI178MC camera and the PC hardware / OS in use.
     * Higher values result in excessive dropped frames.
     */
    asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_BANDWIDTHOVERLOAD, 94, ASI_FALSE);
    if (asi_rtn != ASI_SUCCESS)
    {
        printf("SetControlValue error for ASI_BANDWIDTHOVERLOAD: %d\n", (int)asi_rtn);
        return 1;
    }

    asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_HIGH_SPEED_MODE, 1, ASI_FALSE);
    if (asi_rtn != ASI_SUCCESS)
    {
        printf("SetControlValue error for ASI_HIGH_SPEED_MODE: %d\n", (int)asi_rtn);
        return 1;
    }

    asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_WB_B, 90, ASI_FALSE);
    if (asi_rtn != ASI_SUCCESS)
    {
        printf("SetControlValue error for ASI_WB_B: %d\n", (int)asi_rtn);
        return 1;
    }

    asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_WB_R, 48, ASI_FALSE);
    if (asi_rtn != ASI_SUCCESS)
    {
        printf("SetControlValue error for ASI_WB_R: %d\n", (int)asi_rtn);
        return 1;
    }

    asi_lock.unlock();

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
    printf("Each frame contains %lu bytes\n", image_size_bytes);

    // FIFOs holding pointers to frame buffers
    deque<uint8_t *> unused_frame_deque;
    deque<uint8_t *> frame_deque;

    // Allocate pool of frame buffers
    constexpr size_t FRAME_POOL_SIZE = 64;
    for(int i = 0; i < FRAME_POOL_SIZE; i++)
    {
        unused_frame_deque.push_front(new uint8_t[image_size_bytes]);
    }

    constexpr char FILE_NAME[] = "/home/rgottula/Desktop/test.bin";
    int fd = open(FILE_NAME, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        err(1, "open(%s) failed", FILE_NAME);
    }

    // start thread for writing frame data to disk
    thread write_to_disk_thread(
        write_to_disk,
        fd,
        image_size_bytes,
        ref(frame_deque),
        ref(unused_frame_deque)
    );

    // start thread for AGC
    thread agc_thread(
        agc,
        CamInfo,
        image_size_bytes,
        ref(frame_deque)
    );

    asi_lock.lock();
    asi_rtn = ASIStartVideoCapture(CamInfo.CameraID);
    asi_lock.unlock();
    if (asi_rtn != ASI_SUCCESS)
    {
        printf("StartVideoCapture error: %d\n", (int)asi_rtn);
        return 1;
    }

    int frame_count = 0;
    int time1 = GetTickCount();
    while (true)
    {
        unique_lock<mutex> unused_frame_deque_lock(unused_frame_deque_mutex);
        if (unused_frame_deque.empty())
        {
            unused_frame_deque_lock.unlock();
            usleep(1000);
            continue;
        }
        uint8_t *frame_buffer = unused_frame_deque.back();
        unused_frame_deque.pop_back();
        unused_frame_deque_lock.unlock();
        asi_lock.lock();
        asi_rtn = ASIGetVideoData(CamInfo.CameraID, frame_buffer, image_size_bytes, 200);
        asi_lock.unlock();
        if (asi_rtn == ASI_SUCCESS)
        {
            frame_count++;
        }
        else
        {
            printf("GetVideoData failed with error code %d\n", (int)asi_rtn);
        }
        unique_lock<mutex> frame_deque_lock(frame_deque_mutex);
        frame_deque.push_front(frame_buffer);
        frame_deque_lock.unlock();

        int time2 = GetTickCount();

        if (time2 - time1 > 1000)
        {
            int num_dropped_frames;
            asi_lock.lock();
            ASIGetDroppedFrames(CamInfo.CameraID, &num_dropped_frames);
            asi_lock.unlock();
            printf("frame count: %d dropped frames: %d\n", frame_count, num_dropped_frames);
            printf(
                "The queue has %d frames, the pool has %d free buffers.\n",
                frame_deque.size(),
                unused_frame_deque.size()
            );
            time1 = GetTickCount();
        }
    }

    // this is currently unreachable but keeping it since it shows the proper way to shut down
    asi_lock.lock();
    ASIStopVideoCapture(CamInfo.CameraID);
    ASICloseCamera(CamInfo.CameraID);
    asi_lock.unlock();

    while (frame_deque.empty() == false)
    {
        delete [] frame_deque.back();
        frame_deque.pop_back();
    }

    while (unused_frame_deque.empty() == false)
    {
        delete [] unused_frame_deque.back();
        unused_frame_deque.pop_back();
    }

    return 0;
}