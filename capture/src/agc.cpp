#include "agc.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <deque>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <unistd.h>
#include <spdlog/spdlog.h>
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
    static double agc_value = 0.0;

    spdlog::info("Gain thread id: {}", syscall(SYS_gettid));

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

        // Calculate Nth percentile pixel value
        constexpr float percentile = 1.0;
        uint32_t integral_threshold = (uint32_t)((1.0 - percentile) * Frame::IMAGE_SIZE_BYTES);
        uint8_t upper_tail_val = 255;
        uint32_t integral = hist[upper_tail_val];;
        while (integral <= integral_threshold)
        {
            upper_tail_val--;
            integral += hist[upper_tail_val];
        }

        // Adjust AGC
        if (upper_tail_val >= 255)
        {
            agc_value -= 0.01;
        }
        else if (upper_tail_val < 230)
        {
            agc_value += 0.01;
        }
        agc_value = std::clamp(agc_value, 0.0, 1.0);

        // derive new camera gain
        camera_gain = std::clamp(
            (int)(4.0 * GAIN_MAX * agc_value - (3.0 * GAIN_MAX)),
            GAIN_MIN,
            GAIN_MAX
        );

        // derive new camera exposure time
        camera_exposure_us = std::clamp(
            (int)(4.0 / 3.0 * EXPOSURE_MAX_US * agc_value),
            EXPOSURE_MIN_US,
            EXPOSURE_MAX_US
        );

        spdlog::debug(
            "AGC value: {:.3f}, upper tail value: {:03d}, gain: {:03d}, exposure: {:05.3f} ms",
            agc_value,
            upper_tail_val,
            camera_gain,
            (float)camera_exposure_us / 1.0e3
        );
    }

    spdlog::info("AGC thread ending.");
}
