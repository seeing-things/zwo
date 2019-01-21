#include "disk.h"
#include <deque>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <unistd.h>
#include <sys/syscall.h>
#include "Frame.h"
#include "SERFile.h"


extern std::atomic_bool end_program;

extern std::mutex to_disk_deque_mutex;
extern std::condition_variable to_disk_deque_cv;
extern std::deque<Frame *> to_disk_deque;


// Writes frames of data to disk as quickly as possible. Run as a thread.
void write_to_disk(
    const char *filename,
    int32_t image_width,
    int32_t image_height)
{
    printf("disk thread id: %ld\n", syscall(SYS_gettid));

    std::unique_ptr<SERFile> ser_file;

    bool disk_write_enabled = filename != nullptr;
    if (disk_write_enabled)
    {
        ser_file.reset(
            new SERFile(filename, image_width, image_height, BAYER_RGGB, 8, "", "ZWO ASI178MC", "")
        );
    }
    else
    {
        printf("Warning: No filename provided; not writing to disk!\n");
    }

    while (!end_program)
    {
        // Get next frame from deque
        std::unique_lock<std::mutex> to_disk_deque_lock(to_disk_deque_mutex);
        to_disk_deque_cv.wait(to_disk_deque_lock, [&]{return !to_disk_deque.empty() || end_program;});
        if (end_program)
        {
            break;
        }
        Frame *frame = to_disk_deque.back();
        to_disk_deque.pop_back();
        to_disk_deque_lock.unlock();

        if (disk_write_enabled)
        {
            ser_file->addFrame(*frame);
        }

        frame->decrRefCount();
    }

    printf("Disk thread ending.\n");
}