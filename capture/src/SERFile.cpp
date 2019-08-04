#include "SERFile.h"
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <err.h>


SERFile::SERFile(
    const char *filename,
    int32_t width,
    int32_t height,
    SERColorID_t color_id,
    int32_t bit_depth,
    const char *observer,
    const char *instrument,
    const char *telescope,
    bool add_trailer
) :
    UTC_OFFSET_S(utcOffset()),
    add_trailer_(add_trailer)
{
    bytes_per_frame_ = width * height * ((bit_depth - 1) / 8 + 1);
    if (color_id == RGB || color_id == BGR)
    {
        bytes_per_frame_ *= 3;
    }

    fd_ = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd_ < 0)
    {
        err(1, "open(%s) failed", filename);
    }

    // Extend file size to length of header
    if (ftruncate(fd_, sizeof(SERHeader_t)))
    {
        err(1, "could not extend file to make room for SER header");
    }

    // Reposition file descriptor offset past header
    if (lseek(fd_, 0, SEEK_END) < 0)
    {
        err(1, "could not seek past header in SER file");
    }

    // Map the header portion of the file into memory
    header_ = (SERHeader_t *)mmap(
        0,
        sizeof(SERHeader_t),
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd_,
        0
    );
    if (header_ == MAP_FAILED)
    {
        err(1, "mmap for SERFile header failed");
    }

    // Use placement new operator to init header to defaults in struct definition
    new(header_) SERHeader_t;

    // Init header values based on constructor args
    header_->ImageWidth = width;
    header_->ImageHeight = height;
    header_->ColorID = color_id;
    header_->PixelDepthPerPlane = bit_depth;
    strncpy(header_->Observer, observer, 40);
    strncpy(header_->Instrument, instrument, 40);
    strncpy(header_->Telescope, telescope, 40);
    makeTimestamps(&(header_->DateTime_UTC), &(header_->DateTime));
}

SERFile::~SERFile()
{
    if (add_trailer_)
    {
        if (header_->FrameCount != static_cast<int32_t>(frame_timestamps_.size()))
        {
            warnx("SERFile class frame count %d does not match timestamp vector size %zu",
                header_->FrameCount,
                frame_timestamps_.size()
            );
        }

        size_t trailer_len_bytes = sizeof(int64_t) * frame_timestamps_.size();
        ssize_t n = write(fd_, frame_timestamps_.data(), trailer_len_bytes);
        if (n < 0)
        {
            err(1, "write failed");
        }
        else if (n != static_cast<ssize_t>(trailer_len_bytes))
        {
            err(1, "write incomplete (%zd/%zu)", n, trailer_len_bytes);
        }
    }

    if (munmap(header_, sizeof(SERHeader_t)))
    {
        err(1, "munmap for SERFile header failed");
    }
    (void)close(fd_);
}

void SERFile::addFrame(Frame &frame)
{
    if (bytes_per_frame_ != Frame::IMAGE_SIZE_BYTES)
    {
        errx(
            1,
            "frame size %zu bytes does not match expected size %zu bytes",
            Frame::IMAGE_SIZE_BYTES,
            bytes_per_frame_
        );
    }

    if (add_trailer_)
    {
        int64_t utc_timestamp;
        makeTimestamps(&utc_timestamp, nullptr);
        frame_timestamps_.push_back(utc_timestamp);
    }

    ssize_t n = write(fd_, frame.frame_buffer_, bytes_per_frame_);
    if (n < 0)
    {
        err(1, "write failed");
    }
    else if (n != static_cast<ssize_t>(bytes_per_frame_))
    {
        err(1, "write incomplete (%zd/%zu)", n, bytes_per_frame_);
    }

    header_->FrameCount++;
}

int64_t SERFile::utcOffset()
{
    /*
     * Returns the UTC offset in seconds. This requires ugly methods because the standard
     * libraries as of 2018 do not provide a straightforward way to get this information.
     */
    time_t t = time(nullptr);
    tm *local_time = localtime(&t);
    char tz_str[16];
    strftime(tz_str, 16, "%z", local_time);
    int hours;
    int minutes;
    sscanf(tz_str, "%3d%2d", &hours, &minutes);
    return 3600 * hours + 60 * minutes;
}

void SERFile::makeTimestamps(int64_t *utc, int64_t *local)
{
    using namespace std::chrono;

    /*
     * Number of ticks from the Visual Basic Date data type to the Unix time epoch. The
     * VB Date type is the number of "ticks" since Jan 1, year 0001 in the Gregorian calendar,
     * where each tick is 100 ns.
     */
    constexpr int64_t VB_DATE_TICKS_TO_UNIX_EPOCH = 621'355'968'000'000'000LL;
    constexpr int64_t VB_DATE_TICKS_PER_SEC = 10'000'000LL;

    system_clock::time_point now = system_clock::now();
    int64_t ns_since_epoch = duration_cast<nanoseconds>(now.time_since_epoch()).count();

    int64_t utc_tick = (ns_since_epoch / 100) + VB_DATE_TICKS_TO_UNIX_EPOCH;
    if (utc != nullptr)
    {
        *utc = utc_tick;
    }

    if (local != nullptr)
    {
        *local = utc_tick + UTC_OFFSET_S * VB_DATE_TICKS_PER_SEC;
    }
}
