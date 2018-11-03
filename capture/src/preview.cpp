#include "preview.h"
#include <deque>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <unistd.h>
#include <sys/syscall.h>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include "Frame.h"


extern std::atomic_bool end_program;

extern std::mutex to_preview_deque_mutex;
extern std::condition_variable to_preview_deque_cv;
extern std::deque<Frame *> to_preview_deque;


void preview()
{
    constexpr char WINDOW_NAME[] = "Live Preview";

    printf("preview thread id: %ld\n", syscall(SYS_gettid));

    cv::namedWindow(WINDOW_NAME, cv::WINDOW_NORMAL);
    cv::resizeWindow(WINDOW_NAME, 640, 480);

    while (!end_program)
    {
        // Get frame from deque
        std::unique_lock<std::mutex> to_preview_deque_lock(to_preview_deque_mutex);
        to_preview_deque_cv.wait(
            to_preview_deque_lock,
            [&]{return !to_preview_deque.empty() || end_program;}
        );
        if (end_program)
        {
            break;
        }
        while (to_preview_deque.size() > 1)
        {
            // Discard all but most recent frame
            to_preview_deque.back()->decrRefCount();
            to_preview_deque.pop_back();
        }
        Frame *frame = to_preview_deque.back();
        to_preview_deque.pop_back();
        to_preview_deque_lock.unlock();

        try
        {
            // This throws an exception if the window is closed
            cv::getWindowProperty(WINDOW_NAME, 0);
        }
        catch (cv::Exception &e)
        {
            // window was closed by user
            frame->decrRefCount();
            break;
        }

        cv::Mat img_bayer_bg(2080, 3096, CV_8UC1, (void *)(frame->frame_buffer_));
        cv::Mat img_bgr;
        cv::cvtColor(img_bayer_bg, img_bgr, CV_BayerBG2BGR);
        cv::imshow(WINDOW_NAME, img_bgr);
        cv::waitKey(1);

        frame->decrRefCount();
    }

    printf("Preview thread ending.\n");
}