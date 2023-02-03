#include "disk.h"
#include <deque>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <unistd.h>
#include <err.h>
#include <spdlog/spdlog.h>
#include <sys/syscall.h>
#include <sys/statvfs.h>
#include "Frame.h"


constexpr int64_t MIN_FREE_DISK_SPACE_BYTES = 100 << 20; // 100 MiB

extern std::atomic_bool end_program;

extern std::mutex to_disk_deque_mutex;
extern std::condition_variable to_disk_deque_cv;
extern std::deque<Frame *> to_disk_deque;

extern std::atomic_bool disk_file_exists;
extern std::atomic_bool disk_write_enabled;


// Writes frames of data to disk as quickly as possible. Run as a thread.
void write_to_disk(SERFile *ser_file)
{
    spdlog::info("Disk thread id: {}", syscall(SYS_gettid));

    struct statvfs disk_stats;
    int32_t frame_count = 0;

    while (!end_program)
    {
        // Get next frame from deque
        std::unique_lock<std::mutex> to_disk_deque_lock(to_disk_deque_mutex);
        to_disk_deque_cv.wait(
            to_disk_deque_lock,
            [&]{return !to_disk_deque.empty() || end_program;}
        );
        if (end_program)
        {
            break;
        }
        Frame *frame = to_disk_deque.back();
        to_disk_deque.pop_back();
        to_disk_deque_lock.unlock();

        if (disk_write_enabled && ser_file != nullptr)
        {
            // Check free disk space (but not every single frame)
            if (frame_count % 100 == 0)
            {
                if (statvfs(ser_file->FILENAME.c_str(), &disk_stats) != 0)
                {
                    char buf[256];
                    spdlog::error(
                        "Tried to check disk space with statvfs but the call failed: {}",
                        strerror_r(errno, buf, sizeof(buf))
                    );
                }
                else
                {
                    int64_t free_bytes = disk_stats.f_bsize * disk_stats.f_bavail;
                    if (free_bytes <= MIN_FREE_DISK_SPACE_BYTES)
                    {
                        spdlog::warn(
                            "Disk is nearly full! Disabled writes: frames going to bit bucket!"
                        );
                        disk_write_enabled = false;
                    }
                }
            }

            ser_file->addFrame(*frame);
        }

        frame->decrRefCount();
        frame_count++;
    }

    spdlog::info("Disk thread ending.");
}
