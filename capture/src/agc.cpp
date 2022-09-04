#include "agc.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <deque>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <unistd.h>
#include <sys/syscall.h>
#include "Frame.h"
#include "camera.h"

using namespace camera;

extern std::atomic_bool end_program;

// AGC outputs
extern std::atomic_int camera_gain;
extern std::atomic_int camera_exposure_us;

extern std::mutex to_agc_deque_mutex;
extern std::condition_variable to_agc_deque_cv;
extern std::deque<Frame *> to_agc_deque;

/*
 * This function is intended to be run as a thread. The main thread dispatches frames to this
 * thread via a deque protected by a mutex. For each frame this thread may update either the
 * desired camera gain, exposure time, or both. The new desired values are stored in atomic global
 * variables monitored by the main thread which performs the actual calls to the camera API to
 * commit any changes to hardware.
 */
void agc()
{
    static uint32_t hist[256];

    /*
     * The AGC directly servos this variable which has range [0.0, 1.0]. The camera gain and
     * exposure time are both functions of this value.
     */
    // TODO: no longer matches docstring
    static int agc_value = GAIN_MAX;

    printf("gain thread id: %ld\n", syscall(SYS_gettid));

    while (!end_program)
    {
        // Get frame from deque
        std::unique_lock<std::mutex> to_agc_deque_lock(to_agc_deque_mutex);
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

        // Calculate Nth percentile pixel value (inclusive). This algorithm starts with the
        // integral over the entire histogram, which contains all pixels in the image, and works
        // backwards one histogram bin at a time until the integral from 0 to some pixel value
        // (inclusive) contains `percentile` % of the total number of pixels.
        constexpr float threshold_fraction = 0.99;
        uint32_t integral_threshold = (uint32_t)(threshold_fraction * Frame::IMAGE_SIZE_BYTES);
        uint8_t percentile_value = 255;
        uint32_t integral = Frame::IMAGE_SIZE_BYTES;
        while (integral > integral_threshold)
        {
            integral -= hist[percentile_value];
            percentile_value--;
        }

        printf("%.1f-th percentile value: %d, ", 100*threshold_fraction, percentile_value);


        uint8_t max_pixel_value = 255;
        while (hist[max_pixel_value] == 0)
        {
            max_pixel_value--;
        }

        printf("max pixel value: %d\n", max_pixel_value);

        constexpr uint32_t MAX_SATURATED_PIXELS = 10;
        constexpr uint8_t MIN_MAX_PIXEL_VALUE = 220;
        constexpr int GAIN_STEP = 20;
        if (hist[255] > MAX_SATURATED_PIXELS)
        {
            camera_gain = std::clamp(static_cast<int>(camera_gain - GAIN_STEP), GAIN_MIN, GAIN_MAX);
            printf("changed camera gain to %3d\n", static_cast<int>(camera_gain));
        }
        else if (max_pixel_value < MIN_MAX_PIXEL_VALUE)
        {
            camera_gain = std::clamp(static_cast<int>(camera_gain + GAIN_STEP), GAIN_MIN, GAIN_MAX);
            printf("changed camera gain to %3d\n", static_cast<int>(camera_gain));
        }

        // constexpr double alpha = 0.01;
        // uint8_t target_pixel_value = 200;
        // double error = 0;

        // provide some hysteresis
        // if (max_pixel_value < target_pixel_value - 10 || max_pixel_value == 255)
        // {
        //     if (max_pixel_value == 255)
        //     {
        //         // proportional to number of saturated pixels
        //         // error = GAIN_MAX * static_cast<double>(hist[max_pixel_value]) / Frame::IMAGE_SIZE_BYTES;
        //         error = 50;
        //     }
        //     else
        //     {
        //         error = -std::log(static_cast<double>(target_pixel_value) / max_pixel_value) / alpha;
        //     }

        //     printf("error: %lf\n", error);

        //     constexpr double agc_gain = 0.25;
        //     // if (abs(error) > 20.0)
        //     // {
        //         camera_gain = std::clamp(static_cast<int>(camera_gain - agc_gain * error), GAIN_MIN, GAIN_MAX);
        //         printf("changed camera gain to %3d\n", static_cast<int>(camera_gain));
        //     // }
        // }

        // Adjust AGC
        // int g = camera_gain;
        // printf("gain: %03d", g);
        // if (percentile_value >= 250 && camera_gain > GAIN_MIN)
        // {
        //     camera_gain -= 1;
        //     printf(" -");
        // }
        // else if (percentile_value < 200 && camera_gain < GAIN_MAX)
        // {
        //     camera_gain += 1;
        //     printf(" +");
        // }
        // agc_value = std::clamp(agc_value, GAIN_MIN, GAIN_MAX);

        // printf("AGC value: %.3f, ", agc_value);

        // derive new camera gain
        // int new_gain = agc_value;
        // printf("gain: %03d", new_gain);
        // if (new_gain != camera_gain)
        // {
        //     printf(" %c", (new_gain > camera_gain ? '+' : '-'));
        //     camera_gain = new_gain;
        // }
        printf("\n");

        // // derive new camera exposure time
        // int new_exposure_us = std::clamp(
        //     (int)(4.0 / 3.0 * EXPOSURE_MAX_US * agc_value),
        //     EXPOSURE_MIN_US,
        //     EXPOSURE_MAX_US
        // );
        // printf(", exposure: %05.2f ms", (float)new_exposure_us / 1.0e3);
        // if (new_exposure_us != camera_exposure_us)
        // {
        //     printf(" %c", (new_exposure_us > camera_exposure_us ? '+' : '-'));
        //     camera_exposure_us = new_exposure_us;
        // }

        // printf("\n");
    }

    printf("AGC thread ending.\n");
}
