#pragma once
#include <cstring>
#include <tuple>
#include <vector>
#include "Frame.h"


/*
 * The SER file format is popular in astrophotography for storage of RAW images or video. It is
 * described on this website: http://www.grischa-hahn.homepage.t-online.de/astro/ser/. This class
 * implements version 3 which is documented here:
 * http://www.grischa-hahn.homepage.t-online.de/astro/ser/SER%20Doc%20V3b.pdf
 */


enum SERColorID_t : int32_t
{
    MONO = 0,
    BAYER_RGGB = 8,
    BAYER_GRBG = 9,
    BAYER_GBRG = 10,
    BAYER_BGGR = 11,
    BAYER_CYYM = 16,
    BAYER_YCMY = 17,
    BAYER_YMCY = 18,
    BAYER_MYYC = 19,
    RGB = 100,
    BGR = 101
};


struct [[gnu::packed]] SERHeader_t
{
    // 1. This is a historical artifact of the SER format.
    const char FileID[14] = {'L', 'U', 'C', 'A', 'M', '-', 'R', 'E', 'C', 'O', 'R', 'D', 'E', 'R'};

    // 2. Unused field.
    const int32_t LuID = 0;

    // 3. Identifies how color information is encoded.
    SERColorID_t ColorID = BAYER_RGGB;

    // 4. Set to 1 if 16-bit image data is little-endian. 0 for big-endian.
    int32_t LittleEndian = 0;

    // 5. Width of every image in pixels.
    int32_t ImageWidth = 0;

    // 6. Height of every image in pixels.
    int32_t ImageHeight = 0;

    // 7. Number of bits per pixel per color plane (1-16).
    int32_t PixelDepthPerPlane = 8;

    // 8. Number of image frames in SER file.
    int32_t FrameCount = 0;

    // 9. Name of observer. 40 ASCII characters {32-126 dec.}, fill unused characters with 0 dec.
    char Observer[40];

    // 10. Name of used camera. 40 ASCII characters {32-126 dec.}, fill unused characters with 0 dec.
    char Instrument[40];

    // 11. Name of used telescope. 40 ASCII characters {32-126 dec.}, fill unused characters with 0 dec.
    char Telescope[40];

    // 12. Start time of image stream (local time). Must be >= 0.
    int64_t DateTime = 0;

    // 13. Start time of image stream in UTC.
    int64_t DateTime_UTC = 0;

    SERHeader_t() noexcept
    {
        memset(Observer,   0, sizeof(Observer));
        memset(Instrument, 0, sizeof(Instrument));
        memset(Telescope,  0, sizeof(Telescope));
    }
};


class SERFile
{
public:
    SERFile(
        const char *filename,
        int32_t width,
        int32_t height,
        SERColorID_t color_id = BAYER_RGGB,
        int32_t bit_depth = 8,
        const char *observer = "",
        const char *instrument = "",
        const char *telescope = "",
        bool add_trailer = true
    );
    ~SERFile();

    // Explicit: no copy or move construction or assignment
    SERFile(const SERFile&)            = delete;
    SERFile(SERFile&&)                 = delete;
    SERFile& operator=(const SERFile&) = delete;
    SERFile& operator=(SERFile&&)      = delete;

    void addFrame(Frame &frame);
    static int64_t utcOffset();

    const std::string FILENAME;

private:
    const int64_t UTC_OFFSET_S;
    SERHeader_t *header_;
    int fd_;
    size_t bytes_per_frame_;
    bool add_trailer_;
    std::vector<int64_t> frame_timestamps_;

    using TimestampPair_t = std::tuple<int64_t, int64_t>; // utc, local

    void closeFile();
    TimestampPair_t makeTimestamps();
};
