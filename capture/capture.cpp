#include <cstdio>
#include <cstdint>
#include <deque>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include "ASICamera2.h"

extern unsigned long GetTickCount();

using namespace std;

int main()
{
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
     * Gain initialized to 0. Will be adjusted dynamically with the custom AGC loop in this capture
     * software. The built-in ZWO auto gain feature is disabled.
     */
    asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_GAIN, 0, ASI_FALSE);
    if (asi_rtn != ASI_SUCCESS)
    {
        printf("SetControlValue error for ASI_GAIN: %d\n", (int)asi_rtn);
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

    asi_rtn = ASIStartVideoCapture(CamInfo.CameraID);
    if (asi_rtn != ASI_SUCCESS)
    {
        printf("StartVideoCapture error: %d\n", (int)asi_rtn);
        return 1;
    }

    int frame_count = 0;
    int time1 = GetTickCount();
    while (true)
    {
        if (unused_frame_deque.empty())
        {
            printf("Frame buffer pool exhausted.\n");
            return 1;
        }
        uint8_t *frame_buffer = unused_frame_deque.back();
        unused_frame_deque.pop_back();
        asi_rtn = ASIGetVideoData(CamInfo.CameraID, frame_buffer, image_size_bytes, 200);
        if (asi_rtn == ASI_SUCCESS)
        {
            frame_count++;
        }
        else
        {
            printf("GetVideoData failed with error code %d\n", (int)asi_rtn);
        }
        frame_deque.push_front(frame_buffer);

        printf("The deque has %d frames, the pool has %d free buffers.\n", frame_deque.size(), unused_frame_deque.size());

        if (frame_deque.empty() == false)
        {
            frame_buffer = frame_deque.back();
            frame_deque.pop_back();
            ssize_t n = write(fd, frame_buffer, image_size_bytes);
            if (n < 0)
            {
                err(1, "write failed");
            }
            else if (n != image_size_bytes)
            {
                err(1, "write incomplete (%zd/%zu)", n, image_size_bytes);
            }
            unused_frame_deque.push_front(frame_buffer);
        }


        int time2 = GetTickCount();

        if (time2 - time1 > 1000)
        {
            int num_dropped_frames;
            ASIGetDroppedFrames(CamInfo.CameraID, &num_dropped_frames);
            printf("frame count: %d dropped frames: %d\n", frame_count, num_dropped_frames);
            time1 = GetTickCount();
        }
    }

    // this is currently unreachable but keeping it since it shows the proper way to shut down
    ASIStopVideoCapture(CamInfo.CameraID);
    ASICloseCamera(CamInfo.CameraID);

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