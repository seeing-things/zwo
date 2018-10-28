#include <stdio.h>
#include "ASICamera2.h"

extern unsigned long GetTickCount();

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

    int image_size_bytes;
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
    printf("Each frame contains %d bytes\n", image_size_bytes);

    // TODO: Use a more flexible data structure that can act as a FIFO of frames
    unsigned char *frame_data = new unsigned char[image_size_bytes];

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
        asi_rtn = ASIGetVideoData(CamInfo.CameraID, frame_data, image_size_bytes, 200);
        if (asi_rtn == ASI_SUCCESS)
        {
            frame_count++;
        }
        else
        {
            printf("GetVideoData failed with error code %d\n", (int)asi_rtn);
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
    delete [] frame_data;
    return 0;
}