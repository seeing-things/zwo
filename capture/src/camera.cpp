#include "camera.h"
#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <unistd.h>
#include <sys/syscall.h>
#include <err.h>
#include "Frame.h"


// All threads should end gracefully when this is true
extern std::atomic_bool end_program;

// AGC enable state
extern std::atomic_bool agc_enabled;

// AGC outputs
extern std::atomic_int camera_gain;
extern std::atomic_int camera_exposure_us;

// std::deque is not thread safe
extern std::mutex to_disk_deque_mutex;
extern std::mutex to_preview_deque_mutex;
extern std::mutex to_agc_deque_mutex;
extern std::mutex unused_deque_mutex;

extern std::condition_variable to_disk_deque_cv;
extern std::condition_variable to_preview_deque_cv;
extern std::condition_variable to_agc_deque_cv;
extern std::condition_variable unused_deque_cv;

// FIFOs holding pointers to frame objects
extern std::deque<Frame *> to_disk_deque;
extern std::deque<Frame *> to_preview_deque;
extern std::deque<Frame *> to_agc_deque;
extern std::deque<Frame *> unused_deque;


// Defined by libASICamera2, returns a tick in milliseconds.
extern unsigned long GetTickCount();


static const char *asi_error_str(ASI_ERROR_CODE code)
{
    switch (code)
    {
#define _ASI_ERROR_STR(_code) case _code: return #_code
    _ASI_ERROR_STR(ASI_SUCCESS);
    _ASI_ERROR_STR(ASI_ERROR_INVALID_INDEX);
    _ASI_ERROR_STR(ASI_ERROR_INVALID_ID);
    _ASI_ERROR_STR(ASI_ERROR_INVALID_CONTROL_TYPE);
    _ASI_ERROR_STR(ASI_ERROR_CAMERA_CLOSED);
    _ASI_ERROR_STR(ASI_ERROR_CAMERA_REMOVED);
    _ASI_ERROR_STR(ASI_ERROR_INVALID_PATH);
    _ASI_ERROR_STR(ASI_ERROR_INVALID_FILEFORMAT);
    _ASI_ERROR_STR(ASI_ERROR_INVALID_SIZE);
    _ASI_ERROR_STR(ASI_ERROR_INVALID_IMGTYPE);
    _ASI_ERROR_STR(ASI_ERROR_OUTOF_BOUNDARY);
    _ASI_ERROR_STR(ASI_ERROR_TIMEOUT);
    _ASI_ERROR_STR(ASI_ERROR_INVALID_SEQUENCE);
    _ASI_ERROR_STR(ASI_ERROR_BUFFER_TOO_SMALL);
    _ASI_ERROR_STR(ASI_ERROR_VIDEO_MODE_ACTIVE);
    _ASI_ERROR_STR(ASI_ERROR_EXPOSURE_IN_PROGRESS);
    _ASI_ERROR_STR(ASI_ERROR_GENERAL_ERROR);
    _ASI_ERROR_STR(ASI_ERROR_INVALID_MODE);
#undef _ASI_ERROR_STR

    case ASI_ERROR_END: break;
    }

    static thread_local char buf[128];
    snprintf(buf, sizeof(buf), "(ASI_ERROR_CODE)%d", (int)code);
    return buf;
}


void camera::init_camera(ASI_CAMERA_INFO &CamInfo, const char *cam_name, int binning)
{
    ASI_ERROR_CODE asi_rtn;

    int num_devices = ASIGetNumOfConnectedCameras();
    if (num_devices <= 0)
    {
        errx(1, "No cameras connected.");
    }
    else
    {
        if (cam_name == nullptr) {
            asi_rtn = ASIGetCameraProperty(&CamInfo, 0);
            if (asi_rtn != ASI_SUCCESS) {
                errx(1, "ASIGetCameraProperty error: %s", asi_error_str(asi_rtn));
            }

            warnx("Found %d cameras; arbitrarily selecting %s.",
                num_devices, CamInfo.Name);
        } else {
            // TODO: check both ASIGetID and ASIGetSerialNumber with our cameras
            // and see if they (a) work, and (b) are useful for camera selection.

            std::vector<ASI_CAMERA_INFO> matches;
            for (int i = 0; i < num_devices; ++i) {
                ASI_CAMERA_INFO info;

                asi_rtn = ASIGetCameraProperty(&info, i);
                if (asi_rtn != ASI_SUCCESS) {
                    errx(1, "ASIGetCameraProperty error: %s", asi_error_str(asi_rtn));
                }

                if (strcasestr(info.Name, cam_name) != nullptr) {
                    matches.push_back(info);
                }
            }

            if (matches.empty()) {
                errx(1, "Found %d cameras; no camera matched \"%s\"",
                    num_devices, cam_name);
            } else if (matches.size() != 1) {
                warnx("Found %d cameras; %zu different cameras matched \"%s\":",
                    num_devices, matches.size(), cam_name);
                for (const auto& info : matches) {
                    warnx("- %s", info.Name);
                }
                exit(1);
            }

            memcpy(&CamInfo, &matches.front(), sizeof(CamInfo));
            warnx("Found %d cameras; exactly one camera matched \"%s\": %s",
                num_devices, cam_name, CamInfo.Name);
        }
    }

    asi_rtn = ASIOpenCamera(CamInfo.CameraID);
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "OpenCamera error: %s", asi_error_str(asi_rtn));
    }

    asi_rtn = ASIInitCamera(CamInfo.CameraID);
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "InitCamera error: %s", asi_error_str(asi_rtn));
    }

    asi_rtn = ASISetROIFormat(
        CamInfo.CameraID,
        CamInfo.MaxWidth / binning,
        CamInfo.MaxHeight / binning,
        binning,
        ASI_IMG_RAW8
    );
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "SetROIFormat error: %s", asi_error_str(asi_rtn));
    }

    /*
     * Experimentation has shown that the highest value for the BANDWIDTHOVERLOAD parameter that
     * results in stable performance is 94 for the ASI178MC camera and the PC hardware / OS in use.
     * Higher values result in excessive dropped frames.
     */
    asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_BANDWIDTHOVERLOAD, 93, ASI_FALSE);
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "SetControlValue error for ASI_BANDWIDTHOVERLOAD: %s", asi_error_str(asi_rtn));
    }

    asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_HIGH_SPEED_MODE, 1, ASI_FALSE);
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "SetControlValue error for ASI_HIGH_SPEED_MODE: %s", asi_error_str(asi_rtn));
    }
}


void camera::run_camera(ASI_CAMERA_INFO &CamInfo)
{
    constexpr int AGC_PERIOD_MS = 100;
    ASI_ERROR_CODE asi_rtn;

    asi_rtn = ASIStartVideoCapture(CamInfo.CameraID);
    if (asi_rtn != ASI_SUCCESS)
    {
        errx(1, "StartVideoCapture error: %s", asi_error_str(asi_rtn));
    }

    int frame_count = 0;
    int gain_prev = -1;
    int exposure_us_prev = -1;
    int time1 = GetTickCount();
    int agc_last_dispatch_ts = GetTickCount();
    while (!end_program)
    {
        // Get pointer to an available Frame object
        std::unique_lock<std::mutex> unused_deque_lock(unused_deque_mutex);
        while (unused_deque.empty() && !end_program)
        {
            warnx("Frame pool exhausted. :( Frames will likely be dropped.");
            unused_deque_cv.wait(unused_deque_lock);
        }
        if (end_program)
        {
            break;
        }
        Frame *frame = unused_deque.back();
        unused_deque.pop_back();
        unused_deque_lock.unlock();

        // Matching decrement in write_to_disk thread (except in case of failure to get frame data)
        frame->incrRefCount();

        // Set camera gain if value was updated in another thread
        if (camera_gain != gain_prev)
        {
            asi_rtn = ASISetControlValue(CamInfo.CameraID, ASI_GAIN, camera_gain, ASI_FALSE);
            if (asi_rtn != ASI_SUCCESS)
            {
                warnx("SetControlValue error for ASI_GAIN: %s", asi_error_str(asi_rtn));
            }
            gain_prev = camera_gain;
        }

        // Set exposure time if value was updated in another thread
        if (camera_exposure_us != exposure_us_prev)
        {
            asi_rtn = ASISetControlValue(
                CamInfo.CameraID,
                ASI_EXPOSURE,
                camera_exposure_us,
                ASI_FALSE
            );
            if (asi_rtn != ASI_SUCCESS)
            {
                warnx("SetControlValue error for ASI_EXPOSURE: %s", asi_error_str(asi_rtn));
            }
            exposure_us_prev = camera_exposure_us;
        }

        // Populate frame buffer with data from camera
        asi_rtn = ASIGetVideoData(
            CamInfo.CameraID,
            const_cast<uint8_t *>(frame->frame_buffer_),
            Frame::IMAGE_SIZE_BYTES,
            200
        );
        if (asi_rtn == ASI_SUCCESS)
        {
            frame_count++;

            // Dispatch a subset of frames to AGC thread
            if (agc_enabled)
            {
                int now_ts = GetTickCount();
                if (now_ts - agc_last_dispatch_ts > AGC_PERIOD_MS)
                {
                    agc_last_dispatch_ts = now_ts;

                    // Put this frame in the deque headed for AGC thread
                    frame->incrRefCount();
                    std::unique_lock<std::mutex> to_agc_deque_lock(to_agc_deque_mutex);
                    to_agc_deque.push_front(frame);
                    to_agc_deque_lock.unlock();
                    to_agc_deque_cv.notify_one();
                }
            }

            // Put this frame in the deque headed for live preview thread if the deque is empty
            if (to_preview_deque.empty())
            {
                frame->incrRefCount();
                std::unique_lock<std::mutex> to_preview_deque_lock(to_preview_deque_mutex);
                to_preview_deque.push_front(frame);
                to_preview_deque_lock.unlock();
                to_preview_deque_cv.notify_one();
            }

            // Put this frame in the deque headed for write to disk thread
            std::unique_lock<std::mutex> to_disk_deque_lock(to_disk_deque_mutex);
            to_disk_deque.push_front(frame);
            to_disk_deque_lock.unlock();
            to_disk_deque_cv.notify_one();
        }
        else
        {
            warnx("GetVideoData failed with error code %s", asi_error_str(asi_rtn));
            frame->decrRefCount();
        }

        int time2 = GetTickCount();

        if (time2 - time1 > 1000)
        {
            int num_dropped_frames;
            ASIGetDroppedFrames(CamInfo.CameraID, &num_dropped_frames);
            // printf("Frame count: %06d, Dropped frames: %06d\n", frame_count, num_dropped_frames);
            // printf(
            //     "To-disk queue: %zu frames, to-AGC queue: %zu frames, pool: %zu free frames.\n",
            //     to_disk_deque.size(),
            //     to_agc_deque.size(),
            //     unused_deque.size()
            // );
            time1 = GetTickCount();
        }
    }

    ASIStopVideoCapture(CamInfo.CameraID);
    ASICloseCamera(CamInfo.CameraID);
}
