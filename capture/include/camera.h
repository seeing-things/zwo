#pragma once
#include "ASICamera2.h"

namespace camera
{
    constexpr int WIDTH = 3096;
    constexpr int HEIGHT = 2080;
    constexpr int GAIN_MIN = 0;
    constexpr int GAIN_MAX = 510; // 510 is the maximum value for this camera
    constexpr int EXPOSURE_MIN_US = 32; // 32 is the minimum value for this camera
    constexpr int EXPOSURE_MAX_US = 16'667; // Max for ~60 FPS

    void init_camera(ASI_CAMERA_INFO &CamInfo);
    void run_camera(ASI_CAMERA_INFO &CamInfo);
}
