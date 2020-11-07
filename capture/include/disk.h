#pragma once
#include <cstdint>

void write_to_disk(
    const char *filename,
    const char *camera_name,
    bool color,
    int32_t image_width,
    int32_t image_height
);
