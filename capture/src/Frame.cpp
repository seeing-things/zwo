#include "Frame.h"
#include <deque>
#include <condition_variable>
#include <err.h>
#include <spdlog/spdlog.h>

extern std::deque<Frame *> unused_deque;
extern std::mutex unused_deque_mutex;
extern std::condition_variable unused_deque_cv;

size_t Frame::IMAGE_SIZE_BYTES = 0;
size_t Frame::WIDTH = 0;
size_t Frame::HEIGHT = 0;

Frame::Frame() :
    ref_count_(0)
{
    if (IMAGE_SIZE_BYTES == 0)
    {
        spdlog::critical(
            "Frame: IMAGE_SIZE_BYTES must be set to a non-zero value before construction."
        );
        exit(1);
    }
    frame_buffer_ = new uint8_t[IMAGE_SIZE_BYTES];
    std::unique_lock<std::mutex> unused_deque_lock(unused_deque_mutex);
    unused_deque.push_front(this);
    unused_deque_lock.unlock();
    unused_deque_cv.notify_one();
}

Frame::~Frame()
{
    delete [] frame_buffer_;
}

void Frame::incrRefCount()
{
    // Assume this Frame object has already been removed from the unused frame deque
    ref_count_++;
}

void Frame::decrRefCount()
{
    // Ensure this function is reentrant
    std::lock_guard<std::mutex> lock(decr_mutex_);

    if (ref_count_ <= 0)
    {
        spdlog::critical("Frame.decrRefCount called on Frame where ref_count_ was already zero!");
        exit(1);
    }

    ref_count_--;

    if (ref_count_ == 0)
    {
        std::unique_lock<std::mutex> unused_deque_lock(unused_deque_mutex);
        unused_deque.push_front(this);
        unused_deque_lock.unlock();
        unused_deque_cv.notify_one();
    }
}

uint16_t Frame::syncStart()
{
    // Return first two bytes of the frame buffer
    return (frame_buffer_[0] << 8) | frame_buffer_[1];
}

uint16_t Frame::syncEnd()
{
    // Return last two bytes of the frame buffer
    return (frame_buffer_[IMAGE_SIZE_BYTES - 2] << 8) | frame_buffer_[IMAGE_SIZE_BYTES - 1];
}

uint16_t Frame::frameIndex()
{
    // Return third and fourth bytes of the frame buffer
    return (frame_buffer_[3] << 8) | frame_buffer_[2];
}

bool Frame::validate()
{
    // Valid frames from ASI178 cameras always start and end with these 16-bit values
    constexpr uint16_t SYNC_START = 0x7e5a;
    constexpr uint16_t SYNC_END = 0xf03c;

    auto sync_start = syncStart();
    auto sync_end = syncEnd();

    if ((sync_start == SYNC_START) && (sync_end == SYNC_END)) {
        return true;
    }

    spdlog::error(
        "Bad frame. Started with 0x{:04x} (expected 0x{:04x}) and ended with 0x{:04x} (expected 0x{:04x}).",
        sync_start,
        SYNC_START,
        sync_end,
        SYNC_END);
    return false;
}
