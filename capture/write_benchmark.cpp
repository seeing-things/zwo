// C
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <ctime>

// C++
#include <algorithm>

// Linux
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

// BSD
#include <err.h>


static constexpr size_t round_up_to_multiple(size_t val, size_t mult)
{
    size_t rem = (val % mult);

    if (rem == 0)
    {
        return val;
    }
    else
    {
    //    static_assert((val + (mult - rem)) % mult == 0, "");
        return val + (mult - rem);
    }
}

static uint64_t timespec_to_nanosec(const timespec *tp)
{
    uint64_t ns_sec  = (uint64_t)tp->tv_sec * 1'000'000'000ULL;
    uint64_t ns_nsec = (uint64_t)tp->tv_nsec;

    return ns_sec + ns_nsec;
}

static uint64_t get_timestamp()
{
    timespec t;

    if (clock_gettime(CLOCK_MONOTONIC_RAW, &t) < 0)
    {
        err(1, "clock_gettime failed");
    }

    return timespec_to_nanosec(&t);
}

int main()
{
    constexpr char FILE_NAME[] = "/home/rgottula/Desktop/test.bin";

    // O_DIRECT requires that calls to write(2) have a size that's a multiple
    // of the sector size or whatever; so we specify that sort of thing here
    constexpr size_t ALIGN = 4096;

    constexpr int WIDTH  = 3096;
    constexpr int HEIGHT = 2080;
    constexpr size_t IMAGE_SIZE_BYTES = round_up_to_multiple(WIDTH * HEIGHT, ALIGN);

    auto frame_data = (uint8_t *)aligned_alloc(ALIGN, IMAGE_SIZE_BYTES);

    // Randomize the frame data buffer to prevent any shenanigans (e.g. compression)
    // that the disk might be doing to optimize for certain data patterns
    std::srand(std::time(0));
    std::generate_n(frame_data, IMAGE_SIZE_BYTES, []{ return (uint8_t)std::rand(); });

    // O_DIRECT: bypass the OS's page cache and write directly to the disk
    // O_SYNC:   require every call to write(2) to completely sync to disk before returning
    int flags = O_WRONLY | O_CREAT | O_TRUNC;
    flags |= O_DIRECT;
//    flags |= O_SYNC;

    int fd = open(FILE_NAME, flags, 0644);
    if (fd < 0)
    {
        err(1, "open(%s) failed", FILE_NAME);
    }

    // Make sure that the disk is fully sync'd up before we begin
    (void)syncfs(fd);
    (void)usleep(1 * 1'000'000);

    // Write dummy frames to disk as fast as possible
    constexpr int MAX_FRAMES = 60*10;

    uint64_t timestamps[MAX_FRAMES + 1];
    for (int i = 0; i < MAX_FRAMES; i++)
    {
        timestamps[i] = get_timestamp();

        ssize_t n = write(fd, frame_data, IMAGE_SIZE_BYTES);
        if (n < 0)
        {
            err(1, "write failed");
        }
        else if (n != IMAGE_SIZE_BYTES)
        {
            errx(1, "write incomplete (%zd/%zu)", n, IMAGE_SIZE_BYTES);
        }
    }
    timestamps[MAX_FRAMES] = get_timestamp();

    (void)close(fd);

    double max_period = 0.0;
    double elapsed = (double)(timestamps[MAX_FRAMES] - timestamps[0]) / 1.0e9;
    for (int i = 0; i < MAX_FRAMES; i++)
    {
        double frame_period = (double)(timestamps[i + 1] - timestamps[i]) / 1.0e9;
        max_period = std::max(max_period, frame_period);
        printf("frame %d had period %f ms (%f FPS)\n", i, frame_period * 1.0e3, 1.0 / frame_period);
    }

    printf("max: %f ms, average: %f ms, total elapsed: %f s\n", max_period * 1.0e3, elapsed / (double)MAX_FRAMES * 1.0e3, elapsed);

    return 0;
}
