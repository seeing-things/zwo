#include "stdio.h"
#include "ASICamera2.h"
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>

extern unsigned long GetTickCount();

ASI_CAMERA_INFO CamInfo;

int main()
{
    char* bayer[] = {"RG","BG","GR","GB"};
    char* controls[] = {"Exposure", "Gain", "Gamma", "WB_R", "WB_B", "Brightness", "USB Traffic"};

    int numDevices = ASIGetNumOfConnectedCameras();
    if (numDevices <= 0)
    {
        printf("no camera connected, press any key to exit\n");
        getchar();
        return -1;
    }

    printf("Attached cameras:\n");
    for (int i = 0; i < numDevices; i++)
    {
        ASIGetCameraProperty(&CamInfo, i);
        printf("%d %s\n", i, CamInfo.Name);
    }

    int CamIndex = 0;
    printf("Camera %d selected.\n\n", CamIndex);

    ASIGetCameraProperty(&CamInfo, CamIndex);
    ASI_ERROR_CODE asi_rtn = ASIOpenCamera(CamInfo.CameraID);
    if (asi_rtn != ASI_SUCCESS)
    {
        printf("OpenCamera error\n");
        return -1;
    }

    asi_rtn = ASIInitCamera(CamInfo.CameraID);
    if (asi_rtn != ASI_SUCCESS)
    {
        printf("InitCamera error\n");
        return -1;
    }

    printf("%s information\n", CamInfo.Name);
    printf("Resolution: %dX%d\n", CamInfo.MaxWidth, CamInfo.MaxHeight);
    if (CamInfo.IsColorCam)
        printf("Color Camera: bayer pattern:%s\n", bayer[CamInfo.BayerPattern]);
    else
        printf("Mono camera\n");

    int ctrlnum;
    ASIGetNumOfControls(CamInfo.CameraID, &ctrlnum);
    ASI_CONTROL_CAPS ctrlcap;
    printf("List of controls supported by this camera:\n");
    for (int i = 0; i < ctrlnum; i++)
    {
        ASIGetControlCaps(CamInfo.CameraID, i, &ctrlcap);
        printf("%s\n", ctrlcap.Name);
    }
    printf("\n");

    long lVal;
    ASI_BOOL bAuto;
    ASIGetControlValue(CamInfo.CameraID, ASI_TEMPERATURE, &lVal, &bAuto);
    printf("sensor temperature:%.1f\n", lVal/10.0);


    int bin = 1;
    int width = 3096;
    int height = 2080;
    int Image_type = ASI_IMG_RAW8;
    if (ASISetROIFormat(CamInfo.CameraID, width, height, bin, (ASI_IMG_TYPE)Image_type))
    {
        printf("Problem setting the ROI format\n");
        exit(1);
    }

    int image_size;
    switch (Image_type)
    {
        case ASI_IMG_RAW16:
            image_size = width * height * 2;
            break;
        case ASI_IMG_RGB24:
            image_size = width * height * 3;
            break;
        default:
            image_size = width * height;
    }

    printf("Each frame contains %d bytes\n", image_size);
    printf("\n");

    printf("How high of a speed can you handle?? Enter it here: ");
    int overload = 0;
    scanf("%d", &overload);
    printf("You asked for an overload of: %d\n", overload);

    int exp_ms = 10;
    printf("Using exposure time %d (ms)\n", exp_ms);
    ASISetControlValue(CamInfo.CameraID,ASI_EXPOSURE, exp_ms*1000, ASI_FALSE);
    ASISetControlValue(CamInfo.CameraID,ASI_GAIN,0, ASI_FALSE);
    ASISetControlValue(CamInfo.CameraID,ASI_BANDWIDTHOVERLOAD, overload, ASI_FALSE); //low transfer speed
    printf("Uh oh: High speed mode activated!!!\n");
    ASISetControlValue(CamInfo.CameraID,ASI_HIGH_SPEED_MODE, 1, ASI_FALSE);
    ASISetControlValue(CamInfo.CameraID,ASI_WB_B, 90, ASI_FALSE);
    ASISetControlValue(CamInfo.CameraID,ASI_WB_R, 48, ASI_FALSE);

    ASIStartVideoCapture(CamInfo.CameraID); //start privew


    int time1 = GetTickCount();
    unsigned char *data = (unsigned char*)malloc(image_size);

    int iDropFrame;
    int frameCount = 0;
    int errorCount = 0;
    while (1)
    {
        int retval = ASIGetVideoData(CamInfo.CameraID, data, image_size, (exp_ms <= 100) ? 200 : (exp_ms * 2));
        if (retval == ASI_SUCCESS)
        {
            frameCount++;
        }
        else
        {
            printf("GetVideoData failed with error code %d\n", retval);
            errorCount++;
        }

        int time2 = GetTickCount();

        if (time2 - time1 > 1000)
        {
            ASIGetDroppedFrames(CamInfo.CameraID, &iDropFrame);
            printf("fps:%d dropped frames:%lu, errors: %d\n", frameCount, iDropFrame, errorCount);
            frameCount = 0;
            time1 = GetTickCount();
        }
    }

    // this is currently unreachable but keeping it since it shows the proper way to shut down
    ASIStopVideoCapture(CamInfo.CameraID);
    ASICloseCamera(CamInfo.CameraID);
    free(data);
    printf("main function over\n");
    return 0;
}
