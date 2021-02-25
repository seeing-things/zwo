#include <cstdio>
#include <cstdint>
#include <csignal>
#include <cstring>
#include <deque>
#include <thread>
#include <pthread.h>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <unistd.h>
#include <sys/syscall.h>
#include <err.h>
#include <zwo_fixer.hpp>
#include "Frame.h"
#include "agc.h"
#include "disk.h"
#include "preview.h"
#include "camera.h"


///////////////////////////////////////////////////////////////////////////////////////////////////
// Globals accessed by all threads
///////////////////////////////////////////////////////////////////////////////////////////////////

// All threads should end gracefully when this is true
std::atomic_bool end_program = false;

// AGC enable state
std::atomic_bool agc_enabled = false;

// AGC outputs
std::atomic_int camera_gain = camera::GAIN_MAX;
std::atomic_int camera_exposure_us = camera::EXPOSURE_MAX_US;

// disk thread state
std::atomic_bool disk_file_exists = false;
std::atomic_bool disk_write_enabled = false;

// std::deque is not thread safe
std::mutex to_disk_deque_mutex;
std::mutex to_preview_deque_mutex;
std::mutex to_agc_deque_mutex;
std::mutex unused_deque_mutex;

std::condition_variable to_disk_deque_cv;
std::condition_variable to_preview_deque_cv;
std::condition_variable to_agc_deque_cv;
std::condition_variable unused_deque_cv;

// FIFOs holding pointers to frame objects
std::deque<Frame *> to_disk_deque;
std::deque<Frame *> to_preview_deque;
std::deque<Frame *> to_agc_deque;
std::deque<Frame *> unused_deque;

///////////////////////////////////////////////////////////////////////////////////////////////////
// End globals declaration section
///////////////////////////////////////////////////////////////////////////////////////////////////


static void set_thread_priority(pthread_t thread, int policy, int priority)
{
    sched_param sch_params;
    sch_params.sched_priority = priority;
    if ((errno = pthread_setschedparam(thread, policy, &sch_params)))
    {
        err(1, "Failed to set thread priority (%d, %d)", policy, priority);
    }
}

static void set_thread_name(pthread_t thread, const char *name)
{
    if ((errno = pthread_setname_np(thread, name)))
    {
        warn("Failed to set thread name (\"%s\")", name);
    }
}


void sigint_handler(int signal)
{
    end_program = true;
    to_disk_deque_cv.notify_one();
    to_preview_deque_cv.notify_one();
    to_agc_deque_cv.notify_one();
    unused_deque_cv.notify_one();
}


int main(int argc, char *argv[])
{
    signal(SIGINT, sigint_handler);

    printf("main thread id: %ld\n", syscall(SYS_gettid));

    const char *cam_name = nullptr;
    const char *filename = nullptr;
    int binning = 1;
    for (int i = 1; i < argc; ++i)
    {
        if (strncmp(argv[i], "camera=", 7) == 0)
        {
            cam_name = argv[i] + 7;
        }
        else if (strncmp(argv[i], "file=", 5) == 0)
        {
            filename = argv[i] + 5;
            disk_file_exists = true;
        }
        else if (strncmp(argv[i], "binning=", 8) == 0)
        {
            binning = std::stoi(argv[i] + 8);
        }
        else
        {
            errx(
                1,
                "Error: Program option '%s' not recognized\n"
                "Usage: %s file=[output_filename.ser] camera=[camera name]",
                argv[i], argv[0]
            );
        }
    }

    /*
     * Set real-time priority for the main thread. All threads created later, including by the ASI
     * library, will inherit this priority.
     */
    set_thread_priority(pthread_self(), SCHED_RR, 10);
    set_thread_name(pthread_self(), "capture:main");

    bool zwo_fixer_ok = ZWOFixerInit();
    (void)zwo_fixer_ok; // currently unused

    ASI_CAMERA_INFO CamInfo;
    camera::init_camera(CamInfo, cam_name, binning);

    // Create pool of frame buffers
    Frame::WIDTH = CamInfo.MaxWidth / binning;
    Frame::HEIGHT = CamInfo.MaxHeight / binning;
    Frame::IMAGE_SIZE_BYTES = Frame::WIDTH * Frame::HEIGHT;
    constexpr size_t FRAME_POOL_SIZE = 64;
    static std::deque<Frame> frames;
    for(size_t i = 0; i < FRAME_POOL_SIZE; i++)
    {
        // Frame objects add themselves to unused_deque on construction
        frames.emplace_back();
    }

    // Start threads
    static std::thread write_to_disk_thread(
        write_to_disk,
        filename,
        CamInfo.Name,
        CamInfo.IsColorCam == ASI_TRUE,
        Frame::WIDTH,
        Frame::HEIGHT
    );
    static std::thread preview_thread(preview, CamInfo.IsColorCam == ASI_TRUE);
    static std::thread agc_thread(agc);

    // These threads do not need real-time priority
    set_thread_priority(preview_thread.native_handle(), SCHED_OTHER, 0);
    set_thread_priority(agc_thread.native_handle(), SCHED_OTHER, 0);

    set_thread_name(write_to_disk_thread.native_handle(), "capture:disk");
    set_thread_name(preview_thread.native_handle(), "capture:preview");
    set_thread_name(agc_thread.native_handle(), "capture:gain");

    // Get frames from camera and dispatch them to the other threads
    camera::run_camera(CamInfo);

    printf("Main (camera) thread done, waiting for others to finish.\n");

    write_to_disk_thread.join();
    preview_thread.join();
    agc_thread.join();

    printf("Main thread ending.\n");

    return 0;
}
