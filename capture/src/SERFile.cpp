#include "SERFile.h"
#include <bsd/string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <err.h>
#include <spdlog/spdlog.h>


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
    FILENAME(filename),
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
        char buf[256];
        spdlog::critical("open({}) failed: {}", filename, strerror_r(errno, buf, sizeof(buf)));
        exit(1);
    }

    // Extend file size to length of header
    if (ftruncate(fd_, sizeof(SERHeader_t)))
    {
        char buf[256];
        spdlog::critical(
            "Could not extend file to make room for SER header: {}",
            strerror_r(errno, buf, sizeof(buf))
        );
        exit(1);
    }

    // Reposition file descriptor offset past header
    if (lseek(fd_, 0, SEEK_END) < 0)
    {
        char buf[256];
        spdlog::critical(
            "Could not seek past header in SER file: {}",
            strerror_r(errno, buf, sizeof(buf))
        );
        exit(1);
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
        char buf[256];
        spdlog::critical(
            "mmap for SERFile header failed: {}",
            strerror_r(errno, buf, sizeof(buf))
        );
        exit(1);
    }

    // Use placement new operator to init header to defaults in struct definition
    new(header_) SERHeader_t;

    // Init header values based on constructor args
    header_->ImageWidth = width;
    header_->ImageHeight = height;
    header_->ColorID = color_id;
    header_->PixelDepthPerPlane = bit_depth;
    strlcpy(header_->Observer, observer, 40);
    strlcpy(header_->Instrument, instrument, 40);
    strlcpy(header_->Telescope, telescope, 40);
    auto [utc, local] = makeTimestamps();
    header_->DateTime_UTC = utc;
    header_->DateTime = local;
}

SERFile::~SERFile()
{
    if (header_->FrameCount == 0)
    {
        spdlog::info("Deleting {} since no frames were written to it.", FILENAME);
        closeFile();
        if (remove(FILENAME.c_str()))
        {
            char buf[256];
            spdlog::error(
                "Unable to delete {}: {}",
                FILENAME,
                strerror_r(errno, buf, sizeof(buf))
            );
        }
        return;
    }

    if (add_trailer_)
    {
        if (header_->FrameCount != static_cast<int32_t>(frame_timestamps_.size()))
        {
            auto frame_count = header_->FrameCount;
            spdlog::error("SERFile class frame count {} does not match timestamp vector size {}",
                frame_count,
                frame_timestamps_.size()
            );
        }

        size_t trailer_len_bytes = sizeof(int64_t) * frame_timestamps_.size();
        ssize_t n = write(fd_, frame_timestamps_.data(), trailer_len_bytes);
        if (n < 0)
        {
            char buf[256];
            spdlog::critical(
                "SER file trailer write failed: {}",
                strerror_r(errno, buf, sizeof(buf))
            );
            exit(1);
        }
        else if (n != static_cast<ssize_t>(trailer_len_bytes))
        {
            char buf[256];
            spdlog::critical(
                "SER file trailer write incomplete ({}/{}): {}",
                n,
                trailer_len_bytes,
                strerror_r(errno, buf, sizeof(buf))
            );
            exit(1);
        }
    }

    closeFile();
}

void SERFile::closeFile()
{
    if (munmap(header_, sizeof(SERHeader_t)))
    {
        char buf[256];
        spdlog::critical(
            "munmap for SERFile header failed: {}",
            strerror_r(errno, buf, sizeof(buf))
        );
        exit(1);
    }
    (void)close(fd_);
}

void SERFile::addFrame(Frame &frame)
{
    if (bytes_per_frame_ != Frame::IMAGE_SIZE_BYTES)
    {
        spdlog::error(
            "frame size {} bytes does not match expected size {} bytes",
            Frame::IMAGE_SIZE_BYTES,
            bytes_per_frame_
        );
        exit(1);
    }

    if (add_trailer_)
    {
        int64_t utc_timestamp;
        std::tie(utc_timestamp, std::ignore) = makeTimestamps();
        frame_timestamps_.push_back(utc_timestamp);
    }

    ssize_t n = write(fd_, frame.frame_buffer_, bytes_per_frame_);
    if (n < 0)
    {
        char buf[256];
        spdlog::critical(
            "write failed: {}",
            strerror_r(errno, buf, sizeof(buf))
        );
        exit(1);
    }
    else if (n != static_cast<ssize_t>(bytes_per_frame_))
    {
        char buf[256];
        spdlog::critical(
            "write incomplete ({}/{}): {}",
            n,
            bytes_per_frame_,
            strerror_r(errno, buf, sizeof(buf))
        );
        exit(1);
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

SERFile::TimestampPair_t SERFile::makeTimestamps()
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
    int64_t local_tick = utc_tick + UTC_OFFSET_S * VB_DATE_TICKS_PER_SEC;

    return TimestampPair_t(utc_tick, local_tick);
}
