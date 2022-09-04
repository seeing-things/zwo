/*
 * This was written to help debug some strange behavior with the ASI178MC, where
 * the LSBs of 16-bit data are nonzero, even though the max resolution of the
 * camera sensor is 14 bits. I had taken frames using the SWIG bindings, but I
 * wanted to rule out SWIG as a contributing factor. I also want to experiment
 * with initializing the buffer values to see if perhaps the API is not
 * actually overwriting all of the bits in each 16-bit value.
 */

#include "ASICamera2.h"
#include <err.h>
#include <cstdio>
#include <cstring>
#include <inttypes.h>


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

int main()
{
    // configuration
    int num_pixels = 3096 * 2080;
    int binning = 1;
    ASI_IMG_TYPE img_type = ASI_IMG_RAW16;

    ASI_CAMERA_INFO CamInfo;
    ASI_ERROR_CODE asi_rtn;

    int num_devices = ASIGetNumOfConnectedCameras();
    if (num_devices <= 0)
    {
        errx(1, "No cameras connected.");
    }
    else
    {
        asi_rtn = ASIGetCameraProperty(&CamInfo, 0);
        if (asi_rtn != ASI_SUCCESS) {
            errx(1, "ASIGetCameraProperty error: %s", asi_error_str(asi_rtn));
        }

        warnx("Found %d cameras; arbitrarily selecting %s.",
            num_devices, CamInfo.Name);
    }

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
        CamInfo.MaxWidth / binning,
        CamInfo.MaxHeight / binning,
        binning,
        img_type
    );
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "SetROIFormat error: %s", asi_error_str(asi_rtn));
    }


    asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_HIGH_SPEED_MODE, 0, ASI_FALSE);
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "SetControlValue error for ASI_HIGH_SPEED_MODE: %s", asi_error_str(asi_rtn));
    }

    printf("Starting exposure...");
    ASI_EXPOSURE_STATUS exposure_status;
    asi_rtn = ASIStartExposure(CamInfo.CameraID, ASI_FALSE);
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "ASIStartExposure error: %s", asi_error_str(asi_rtn));
    }
    while (true) {
        asi_rtn = ASIGetExpStatus(CamInfo.CameraID, &exposure_status);
        if (asi_rtn != ASI_SUCCESS)
        {
            errx(1, "ASIGetExpStatus error: %s", asi_error_str(asi_rtn));
        }
        if (exposure_status == ASI_EXP_SUCCESS) {
            break;
        } else if (exposure_status == ASI_EXP_FAILED) {
            errx(1, "Exposure failed.");
        }
    }
    printf("complete.\n");

    uint16_t *buffer = new uint16_t[num_pixels];
    memset(buffer, 0, 2 * num_pixels);
    asi_rtn = ASIGetDataAfterExp(CamInfo.CameraID, (uint8_t *)buffer, 2 * num_pixels);
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "ASIGetDataAfterExp error: %s", asi_error_str(asi_rtn));
    }

    printf("Writing to file...");
    FILE *f = fopen("/tmp/test_image.uint16", "wb");
    fwrite(buffer, sizeof(uint16_t), num_pixels, f);
    fclose(f);
    printf("done.\n");
}
