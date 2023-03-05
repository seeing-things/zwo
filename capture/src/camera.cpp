#include "camera.h"
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <dirent.h>
#include <err.h>
#include <libusb-1.0/libusb.h>
#include <mutex>
#include <spdlog/spdlog.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>
#include "Frame.h"


#define LIBUSB_CHECK(func, ...) \
    do { \
        int ret = func(__VA_ARGS__); \
        if (ret < LIBUSB_SUCCESS) { \
            spdlog::error(#func " returned {}: {}", \
                libusb_error_name(ret), \
                libusb_strerror((libusb_error)ret) \
            ); \
        } \
    } while (0)

#define ASI_EXIT_ON_FAIL(func, ...) \
    do { \
        ASI_ERROR_CODE ret = func(__VA_ARGS__); \
        if (ret != ASI_SUCCESS) { \
            spdlog::error(#func " returned  {}", asi_error_str(ret)); \
            exit(1); \
        } \
    } while (0)

#define ASI_LOG_ON_FAIL(func, ...) \
    do { \
        ASI_ERROR_CODE ret = func(__VA_ARGS__); \
        if (ret != ASI_SUCCESS) { \
            spdlog::error(#func " returned  {}", asi_error_str(ret)); \
        } \
    } while (0)


using namespace std::chrono;
using namespace std::chrono_literals;


constexpr auto AGC_PERIOD = 100ms;
constexpr int NUM_LIBUSB_TRANSFERS = 2;
int frame_count = 0;
uint16_t last_frame_index = 0;
libusb_context *ctx = nullptr;
libusb_device_handle *dev_handle = nullptr;


// All threads should end gracefully when this is true
extern std::atomic_bool end_program;

// Estimated rate of frames received from the camera
extern std::atomic<float> camera_frame_rate;

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


// A pointer to an instance is passed to the libusb transfer callback in transfer->user_data
struct CallbackArgs {
    Frame *frame;
    int completed;
};


struct UsbDevice {
    int bus;
    int dev;
};


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


// Prompt user to pick from one of possibly multiple cameras.
ASI_CAMERA_INFO prompt_user_for_camera()
{
    int num_devices = ASIGetNumOfConnectedCameras();
    ASI_CAMERA_INFO CamInfo;

    int selection;
    while (true) {
        printf("\nSelect from the following cameras:\n");
        for (int i = 0; i < num_devices; i++) {
            ASI_EXIT_ON_FAIL(ASIGetCameraProperty, &CamInfo, i);
            printf("\t%d) %s\n", i, CamInfo.Name);
        }
        printf("\nEnter selection: ");
        int scanned = scanf("%d", &selection);
        if (scanned == 1 && selection >= 0 && selection < num_devices) {
            break;
        }
        printf("Invalid selection.\n");

        // clear out stdin
        while (true) {
            int c = getchar();
            if (c == EOF) {
                exit(1);
            }
            if (c == '\n') {
                break;
            }
        }
    }
    ASI_EXIT_ON_FAIL(ASIGetCameraProperty, &CamInfo, selection);
    spdlog::info("User selected camera {}, named '{}'", selection, CamInfo.Name);

    return CamInfo;
}


ASI_CAMERA_INFO select_camera(const char *cam_name)
{
    ASI_CAMERA_INFO CamInfo;

    int num_devices = ASIGetNumOfConnectedCameras();
    spdlog::info("Found {} cameras connected.", num_devices);
    if (num_devices <= 0) {
        spdlog::critical("No cameras connected.");
        exit(1);
    }

    if (cam_name == nullptr) {
        if (num_devices == 1) {
            ASI_EXIT_ON_FAIL(ASIGetCameraProperty, &CamInfo, 0);
            spdlog::info("Connecting to the only camera available, named '{}'", CamInfo.Name);
        } else {
            CamInfo = prompt_user_for_camera();
        }
    } else {
        std::vector<ASI_CAMERA_INFO> matches;
        for (int i = 0; i < num_devices; ++i) {
            ASI_EXIT_ON_FAIL(ASIGetCameraProperty, &CamInfo, i);
            // checks if second arg is substring of first
            if (strcasestr(CamInfo.Name, cam_name) != nullptr) {
                matches.push_back(CamInfo);
            }
        }

        if (matches.empty()) {
            spdlog::critical("No camera name matched '{}'", cam_name);
            exit(1);
        } else if (matches.size() != 1) {
            spdlog::warn("Multiple camera names contain '{}'", cam_name);
            CamInfo = prompt_user_for_camera();
        } else {
            memcpy(&CamInfo, &matches.front(), sizeof(CamInfo));
            spdlog::info("Found exactly one match with name '{}'", CamInfo.Name);
        }
    }

    return CamInfo;
}


void camera::init_camera(ASI_CAMERA_INFO &CamInfo, const char *cam_name, int binning)
{
    CamInfo = select_camera(cam_name);

    ASI_EXIT_ON_FAIL(ASIOpenCamera, CamInfo.CameraID);
    ASI_EXIT_ON_FAIL(ASIInitCamera, CamInfo.CameraID);
    ASI_EXIT_ON_FAIL(
        ASISetROIFormat,
        CamInfo.CameraID,
        CamInfo.MaxWidth / binning,
        CamInfo.MaxHeight / binning,
        binning,
        ASI_IMG_RAW8
    );

    /*
     * A value of 100 works on the author's PC with the ASI178 camera when using the custom
     * implementation of `ASIStartVideoCapture()` and `ASIGetVideoData()` in this program and when
     * the camera thread runs with realtime priority. With the official ZWO driver code the highest
     * value was about 94, but dropped frames were still fairly common.
     */
    ASI_EXIT_ON_FAIL(ASISetControlValue, CamInfo.CameraID, ASI_BANDWIDTHOVERLOAD, 100, ASI_FALSE);
    ASI_EXIT_ON_FAIL(ASISetControlValue, CamInfo.CameraID, ASI_HIGH_SPEED_MODE, 1, ASI_FALSE);
}


libusb_device_handle *init_libusb()
{
    constexpr uint16_t ZWO_VID = 0x03c3;
    constexpr uint16_t ASI178MC_PID = 0x178a;
    constexpr uint16_t ASI178MM_PID = 0x178c;

    LIBUSB_CHECK(libusb_init, &ctx);

    // Figure out what USB devices this process is already connected to
    std::vector<UsbDevice> open_usb_devices;
    char pathname[PATH_MAX + 1];
    char buf[PATH_MAX + 1];
    for (int i = 0; ; i++) {
        sprintf(pathname, "/proc/self/fd/%d", i);
        ssize_t rtn = readlink(pathname, buf, PATH_MAX);
        if (rtn <= 0) {
            break;
        }
        buf[rtn] = 0;  // add null termination since readlink doesn't
        UsbDevice device;
        if (sscanf(buf, "/dev/bus/usb/%03d/%03d", &device.bus, &device.dev) == 2) {
            spdlog::info("This process is using USB device {:03d} on bus {:03d}",
                device.dev,
                device.bus
            );
            open_usb_devices.push_back(device);
        }
    }

    if (open_usb_devices.size() == 0) {
        spdlog::critical("Unable to detect what USB device the ASI library is using.");
        exit(1);
    }

    // Find a USB device on the system that matches the one already opened
    libusb_device **devices_list;
    ssize_t ret = libusb_get_device_list(ctx, &devices_list);
    if (ret < 0) {
        spdlog::critical("libusb_get_device_list returned {}: {}",
            libusb_error_name(ret),
            libusb_strerror((libusb_error)ret)
        );
        exit(1);
    } else if (ret == 0) {
        spdlog::critical("libusb_get_device_list did not find any devices.");
        exit(1);
    }

    std::vector<libusb_device *> matching_devices;
    for (ssize_t i = 0; i < ret; i++) {
        auto dev = devices_list[i];
        auto bus = libusb_get_bus_number(dev);
        auto port = libusb_get_port_number(dev);
        auto addr = libusb_get_device_address(dev);
        spdlog::debug("Found USB device on bus {:03d} port {:03d} addr {:03d}", bus, port, addr);

        for(auto open_dev : open_usb_devices) {
            if (bus == open_dev.bus && addr == open_dev.dev) {
                matching_devices.push_back(dev);
            }
        }
    }

    if (matching_devices.size() != 1) {
        spdlog::critical(
            "Found {} USB devices that match what the ASI driver is using; expected 1.",
            matching_devices.size()
        );
        exit(1);
    }
    auto matching_dev = matching_devices[0];

    libusb_device_descriptor desc;
    libusb_get_device_descriptor(matching_dev, &desc);
    if (desc.idVendor != ZWO_VID) {
        spdlog::critical("Vendor ID is 0x{:x}, expected 0x{:x} for ZWO", desc.idVendor, ZWO_VID);
        exit(1);
    }
    if (desc.idProduct != ASI178MC_PID && desc.idProduct != ASI178MM_PID) {
        spdlog::critical("Product ID is 0x{:x}, expected 0x{:x} or 0x{:x} for ASI178MC or MM",
            desc.idProduct, ASI178MC_PID, ASI178MM_PID);
        exit(1);
    }

    spdlog::info(
        "USB device {:03d} on bus {:03d} seems to be the correct camera",
        libusb_get_device_address(matching_dev),
        libusb_get_bus_number(matching_dev)
    );

    libusb_device_handle *handle;
    LIBUSB_CHECK(libusb_open, matching_dev, &handle);
    libusb_free_device_list(devices_list, 1);
    return handle;
}


// This is a custom implementation of the startup section of `ASIStartVideoCapture()`
void start_streaming(libusb_device_handle *dev_handle)
{
    LIBUSB_CHECK(libusb_reset_device, dev_handle);

    // Stop streaming
    LIBUSB_CHECK(libusb_control_transfer, dev_handle, 0x40, 0xaa, 0x0, 0x0, nullptr, 0, 200);

    // Set bit 4 of FPGA register 0 to put FPGA in the "stop" state
    unsigned char data;
    LIBUSB_CHECK(libusb_control_transfer, dev_handle, 0xc0, 0xbc, 0x0, 0x0, &data, 1, 500);
    LIBUSB_CHECK(libusb_control_transfer, dev_handle, 0x40, 0xbd, 0x0, data | 0x10, nullptr, 0, 500);

    // Start streaming
    LIBUSB_CHECK(libusb_control_transfer, dev_handle, 0x40, 0xa9, 0x0, 0x0, nullptr, 0, 200);

    // Write 0x6 then 0x0 to sensor chip register 0x3000
    LIBUSB_CHECK(libusb_control_transfer, dev_handle, 0x40, 0xb6, 0x3000, 0x6, nullptr, 0, 500);
    LIBUSB_CHECK(libusb_control_transfer, dev_handle, 0x40, 0xb6, 0x3000, 0x0, nullptr, 0, 500);

    // Clear bit 4 of FPGA register 0 to take FPGA out of the "stop" state
    LIBUSB_CHECK(libusb_control_transfer, dev_handle, 0xc0, 0xbc, 0x0, 0x0, &data, 1, 500);
    LIBUSB_CHECK(libusb_control_transfer, dev_handle, 0x40, 0xbd, 0x0, data & 0xef, nullptr, 0, 500);

    // Make sure the bulk transfer endpoint isn't halted
    LIBUSB_CHECK(libusb_clear_halt, dev_handle, 0x81);
}


void libusb_callback(libusb_transfer *transfer)
{
    static auto agc_last_dispatch_ts = steady_clock::now();
    static auto stats_last_printed_ts = steady_clock::now();
    // deque of timestamps for use in calculating frame rate
    constexpr int NUM_FRAMERATE_FRAMES = 100;
    static std::deque<steady_clock::time_point> timestamps(
        NUM_FRAMERATE_FRAMES,
        steady_clock::now()
    );

    CallbackArgs *args = (CallbackArgs *)(transfer->user_data);
    auto frame = args->frame;

    // This is checked by libusb_handle_events_completed() to determine if it needs to wait for
    // this callback event or if it already happened.
    args->completed = 1;

    // I'm not 100% sure all of these transfer errors are handled correctly, in part because I'm
    // not sure how to induce most of them. I can induce an overflow by inserting sleep statements,
    // but I'm less certain about the others. So in many cases the response is to log the event,
    // try to continue and hope for the best.
    switch (transfer->status) {
        case LIBUSB_TRANSFER_COMPLETED:
            break;
        case LIBUSB_TRANSFER_ERROR:
            spdlog::error("LIBUSB_TRANSFER_ERROR");
            frame->decrRefCount();
            return;
        case LIBUSB_TRANSFER_TIMED_OUT:
            spdlog::error("LIBUSB_TRANSFER_TIMED_OUT");
            frame->decrRefCount();
            return;
        case LIBUSB_TRANSFER_CANCELLED:
            spdlog::error("LIBUSB_TRANSFER_CANCELLED");
            frame->decrRefCount();
            return;
        case LIBUSB_TRANSFER_STALL:
            spdlog::error("LIBUSB_TRANSFER_STALL");
            frame->decrRefCount();
            LIBUSB_CHECK(libusb_clear_halt, dev_handle, 0x81);
            return;
        case LIBUSB_TRANSFER_NO_DEVICE:
            spdlog::critical("LIBUSB_TRANSFER_NO_DEVICE");
            exit(1);
        case LIBUSB_TRANSFER_OVERFLOW:
            spdlog::error("LIBUSB_TRANSFER_OVERFLOW");
            frame->decrRefCount();
            // libusb docs say pending transfers should be cancelled before clearing a halt, but
            // this seems to be working fine without doing that.
            LIBUSB_CHECK(libusb_clear_halt, dev_handle, 0x81);
            return;
    }

    if (transfer->length != transfer->actual_length) {
        spdlog::error("Expected {} bytes from USB bulk transfer but got {} (diff: {})",
            transfer->length, transfer->actual_length, transfer->actual_length - transfer->length
        );
    }
    frame->validate();

    // Not sure why the frame index sometimes increments by 2, but even at low frame rates this
    // seems to be true so increment by 1 or 2 are both considered valid.
    auto frame_index = frame->frameIndex();
    if ((frame_index <= last_frame_index) || (frame_index > last_frame_index + 2)) {
        spdlog::warn(
            "Expected frame index {} or {} but got {}",
            last_frame_index + 1,
            last_frame_index + 2,
            frame_index
        );
    }
    last_frame_index = frame_index;

    frame_count++;

    // Dispatch a subset of frames to AGC thread
    if (agc_enabled)
    {
        auto now_ts = steady_clock::now();
        if (now_ts - agc_last_dispatch_ts > AGC_PERIOD)
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

    // Put this frame in the deque headed for write to disk thread. This must be done after
    // dispatching frames to the other threads (AGC, preview) because this thread could decrement
    // the reference count of the frame down to zero before it is processed by those other threads.
    std::unique_lock<std::mutex> to_disk_deque_lock(to_disk_deque_mutex);
    to_disk_deque.push_front(frame);
    to_disk_deque_lock.unlock();
    to_disk_deque_cv.notify_one();

    // For calculating frame rate
    timestamps.push_front(steady_clock::now());
    timestamps.pop_back();
    auto now = timestamps.front();
    auto then = timestamps.back();
    duration<float> elapsed = now - then;
    camera_frame_rate = (float)(NUM_FRAMERATE_FRAMES - 1) / elapsed.count();

    now = steady_clock::now();
    if (now - stats_last_printed_ts > 1s)
    {
        spdlog::info(
            "{:6d} frames, {:6.2f} FPS over last {}",
            frame_count,
            camera_frame_rate,
            NUM_FRAMERATE_FRAMES
        );
        spdlog::debug(
            "Frame counts: To-disk queue: {}, to-AGC queue: {}, to-preview queue: {}, pool: {} free frames.",
            to_disk_deque.size(),
            to_agc_deque.size(),
            to_preview_deque.size(),
            unused_deque.size()
        );
        stats_last_printed_ts = steady_clock::now();
    }
}


void camera::run_camera(ASI_CAMERA_INFO &CamInfo)
{
    dev_handle = init_libusb();
    libusb_transfer *transfers[NUM_LIBUSB_TRANSFERS];
    CallbackArgs callback_args[NUM_LIBUSB_TRANSFERS];

    for (int i = 0; i < NUM_LIBUSB_TRANSFERS; i++) {
        transfers[i] = libusb_alloc_transfer(0);
        if (transfers[i] == nullptr) {
            spdlog::critical("libusb_alloc_transfer returned NULL");
            exit(1);
        }
        libusb_fill_bulk_transfer(
            transfers[i],
            dev_handle,
            0x81,
            nullptr,  // to be filled later
            Frame::IMAGE_SIZE_BYTES,
            libusb_callback,
            &callback_args[i],
            100  // timeout [ms]
        );
    }

    start_streaming(dev_handle);

    // get things started
    std::unique_lock<std::mutex> unused_deque_lock(unused_deque_mutex);
    for (int i = 0; i < NUM_LIBUSB_TRANSFERS; i++) {
        Frame *frame = unused_deque.back();
        unused_deque.pop_back();
        // Matching decrement in write_to_disk thread (except in case of failure to get frame data)
        frame->incrRefCount();
        auto args = (CallbackArgs *)transfers[i]->user_data;
        args->completed = 0;
        args->frame = frame;
        transfers[i]->buffer = const_cast<uint8_t *>(frame->frame_buffer_);
        LIBUSB_CHECK(libusb_submit_transfer, transfers[i]);
    }
    unused_deque_lock.unlock();

    int gain_prev = -1;
    int exposure_us_prev = -1;
    while (!end_program)
    {
        for (int i = 0; i < NUM_LIBUSB_TRANSFERS; i++) {

            auto args = (CallbackArgs *)transfers[i]->user_data;
            LIBUSB_CHECK(libusb_handle_events_completed, ctx, &args->completed);

            // Get pointer to an available Frame object
            std::unique_lock<std::mutex> unused_deque_lock(unused_deque_mutex);
            while (unused_deque.empty() && !end_program)
            {
                spdlog::error("Frame pool exhausted. To-disk queue: {}, to-AGC queue: {}, "
                    "to-preview queue: {}, free: {}.",
                    to_disk_deque.size(),
                    to_agc_deque.size(),
                    to_preview_deque.size(),
                    unused_deque.size()
                );
                unused_deque_cv.wait(unused_deque_lock);
            }
            if (end_program)
            {
                break;
            }
            Frame *frame = unused_deque.back();
            unused_deque.pop_back();
            unused_deque_lock.unlock();

            // Matching decrement in write_to_disk thread (except in case of transfer failure)
            frame->incrRefCount();
            args->completed = 0;
            args->frame = frame;
            transfers[i]->buffer = const_cast<uint8_t *>(frame->frame_buffer_);
            LIBUSB_CHECK(libusb_submit_transfer, transfers[i]);

            // Set camera gain if value was updated in another thread
            if (camera_gain != gain_prev)
            {
                ASI_LOG_ON_FAIL(
                    ASISetControlValue,
                    CamInfo.CameraID,
                    ASI_GAIN,
                    camera_gain,
                    ASI_FALSE
                );
                gain_prev = camera_gain;
                spdlog::info("Camera gain set to {:03d}", camera_gain);
            }

            // Set exposure time if value was updated in another thread
            if (camera_exposure_us != exposure_us_prev)
            {
                ASI_LOG_ON_FAIL(
                    ASISetControlValue,
                    CamInfo.CameraID,
                    ASI_EXPOSURE,
                    camera_exposure_us,
                    ASI_FALSE
                );
                exposure_us_prev = camera_exposure_us;
                spdlog::info(
                    "Camera exposure time set to {:6.3f} ms",
                    (float)exposure_us_prev / 1.0e3
                );
            }
        }
    }

    libusb_close(dev_handle);
    ASICloseCamera(CamInfo.CameraID);
}
