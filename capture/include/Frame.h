#pragma once
#include <cstddef>
#include <mutex>
#include <atomic>

class Frame
{
public:
    Frame();
    ~Frame();

    // Explicit: no copy or move construction or assignment
    Frame(const Frame&)            = delete;
    Frame(Frame&&)                 = delete;
    Frame& operator=(const Frame&) = delete;
    Frame& operator=(Frame&&)      = delete;

    void incrRefCount();
    void decrRefCount();

    // Must be initialized before first object is constructed
    static size_t IMAGE_SIZE_BYTES;
    static size_t WIDTH;
    static size_t HEIGHT;

    // Raw image data from camera
    const uint8_t *frame_buffer_;

private:
    std::atomic_int ref_count_;
    std::mutex decr_mutex_;
};
