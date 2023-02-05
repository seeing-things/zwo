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
#include <spdlog/spdlog.h>
#include <sys/syscall.h>
#include <err.h>
#include "Frame.h"
#include "agc.h"
#include "disk.h"
#include "preview.h"
#include "camera.h"
#include "SERFile.h"


/*
 * This is the total number of Frame objects (frame buffers) that will be allocated. A larger
 * number increases memory usage but decreases the risk that the pool of available frames runs out
 * if for example the to_disk_queue gets backed up momentarily.
 */
constexpr size_t FRAME_POOL_SIZE = 64;


///////////////////////////////////////////////////////////////////////////////////////////////////
// Globals accessed by all threads
///////////////////////////////////////////////////////////////////////////////////////////////////

// All threads should end gracefully when this is true
std::atomic_bool end_program = false;

// Estimated rate of frames received from the camera
std::atomic<float> camera_frame_rate = 0.0;

// AGC enable state
std::atomic_bool agc_enabled = false;

// AGC outputs
std::atomic_int camera_gain = camera::GAIN_MAX;
std::atomic_int camera_exposure_us = camera::EXPOSURE_DEFAULT_US;

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
        char buf[256];
        spdlog::critical("Failed to set thread priority to policy {}, priority {}: {}",
            policy,
            priority,
            strerror_r(errno, buf, sizeof(buf))
        );
        exit(1);
    }
}

static void set_thread_name(pthread_t thread, const char *name)
{
    if ((errno = pthread_setname_np(thread, name)))
    {
        char buf[256];
        spdlog::error(
            "Failed to set thread name to '{}': {}",
            name,
            strerror_r(errno, buf, sizeof(buf))
        );
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


void check_if_file_exists(const char *filename)
{
    if (access(filename, F_OK) == -1)
    {
        // the file doesn't already exist, so we're okay to create a new one
        disk_file_exists = true;
        return;
    }

    while (true) {
        printf("%s already exists. Do you want to overwrite it? [y/N] ", filename);
        char selection;
        int scanned = scanf("%c", &selection);
        if (scanned == 1) {
            if (selection == 'y' || selection == 'Y') {
                spdlog::info("User approved overwriting {}.", filename);
                disk_file_exists = true;
                return;
            } else if (selection == 'n' || selection == 'N' || selection == '\n') {
                spdlog::critical("File {} exists and user declined to overwrite it.", filename);
                exit(1);
            }
        }
        printf("Invalid selection.\n");

        // clear out stdin
        while (true) {
            int c = getchar();
            if (c == EOF) {
                exit(1);
            }
            if (c == '\n') {
                break;
            }
        }
    }
}


int main(int argc, char *argv[])
{
    signal(SIGINT, sigint_handler);

    spdlog::info("Main (camera) thread id: {}", syscall(SYS_gettid));

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

    // libasicamera2 threads will inherit this name
    set_thread_name(pthread_self(), "libasicamera2");
    ASI_CAMERA_INFO CamInfo;
    camera::init_camera(CamInfo, cam_name, binning);
    set_thread_name(pthread_self(), "camera(main)");

    // Create pool of frame buffers
    Frame::WIDTH = CamInfo.MaxWidth / binning;
    Frame::HEIGHT = CamInfo.MaxHeight / binning;
    Frame::IMAGE_SIZE_BYTES = Frame::WIDTH * Frame::HEIGHT;
    static std::deque<Frame> frames;
    for(size_t i = 0; i < FRAME_POOL_SIZE; i++)
    {
        // Frame objects add themselves to unused_deque on construction
        frames.emplace_back();
    }

    std::unique_ptr<SERFile> ser_file;
    if (filename != nullptr) {
        check_if_file_exists(filename);
        ser_file.reset(new SERFile(
            filename,
            Frame::WIDTH,
            Frame::HEIGHT,
            (CamInfo.IsColorCam == ASI_TRUE) ? BAYER_RGGB : MONO,
            8,
            "",
            CamInfo.Name,
            ""
        ));
        spdlog::info("Creating output file {}.", filename);
    } else {
        spdlog::info("No output SER filename provided.");
    }

    // Start threads
    static std::thread write_to_disk_thread(write_to_disk, ser_file.get());
    static std::thread preview_thread(preview, CamInfo.IsColorCam == ASI_TRUE);
    static std::thread agc_thread(agc);

    // Set real-time priority for latency-sensitive threads.
    set_thread_priority(pthread_self(), SCHED_RR, 10);
    set_thread_priority(write_to_disk_thread.native_handle(), SCHED_RR, 10);

    set_thread_name(write_to_disk_thread.native_handle(), "disk");
    set_thread_name(preview_thread.native_handle(), "preview");
    set_thread_name(agc_thread.native_handle(), "agc");

    // Get frames from camera and dispatch them to the other threads
    camera::run_camera(CamInfo);

    spdlog::info("Main (camera) thread done, waiting for others to finish.");

    write_to_disk_thread.join();
    preview_thread.join();
    agc_thread.join();

    spdlog::info("Main thread ending.");

    return 0;
}
