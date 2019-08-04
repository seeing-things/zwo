#include "Frame.h"
#include <deque>
#include <condition_variable>
#include <err.h>

extern std::deque<Frame *> unused_deque;
extern std::mutex unused_deque_mutex;
extern std::condition_variable unused_deque_cv;

size_t Frame::IMAGE_SIZE_BYTES = 0;

Frame::Frame() :
    ref_count_(0)
{
    if (IMAGE_SIZE_BYTES == 0)
    {
        errx(1, "Frame: IMAGE_SIZE_BYTES must be set to a non-zero value before construction");
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
        errx(1, "Frame.decrRefCount called on Frame where ref_count_ was already zero!");
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
