// HOW TO BUILD:
// g++ -std=c++17 -fPIC -shared -Wl,--no-undefined -fuse-ld=gold -ldl -lbsd -lusb-1.0 -Wall -O0 -g3 -o libusb_wrapper.so libusb_wrapper.cpp

// HOW TO USE:
// LD_PRELOAD=libusb_wrapper.so <program command line>


#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <bsd/string.h>

#include <libusb-1.0/libusb.h>

#include <mutex>
#include <map>


#ifdef __cplusplus
#define libusb_strerror(errcode) libusb_strerror(static_cast<enum libusb_error>(errcode))
#endif


// https://en.wikipedia.org/wiki/ANSI_escape_code#Colors
enum
{
	C_BRIGHT_RED     = 91,
	C_BRIGHT_GREEN   = 92,
	C_BRIGHT_YELLOW  = 93,
	C_BRIGHT_BLUE    = 94,
	C_BRIGHT_MAGENTA = 95,
	C_BRIGHT_CYAN    = 96,
};

static void msg(int color, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void msg(int color, const char *fmt, ...)
{
	char fmt_color[8192];
	snprintf(fmt_color, sizeof(fmt_color), "\e[%dm" "%s" "\e[0m", color, fmt);

	va_list va;
	va_start(va, fmt);
	vfprintf(stderr, fmt_color, va);
	va_end(va);
}


#define SETUP(fname) \
	do { \
		if (REAL__##fname == NULL) { \
			REAL__##fname = reinterpret_cast<decltype(REAL__##fname)>(dlsym(RTLD_NEXT, #fname)); \
			if (REAL__##fname == NULL) { \
				msg(C_BRIGHT_RED, "libusb_wrapper: dlsym(RTLD_NEXT, " #fname ") returned NULL! dlerror() says: \"%s\"\n", dlerror()); \
				exit(1); \
			} \
		} \
	} while (false)


static int (*REAL__libusb_clear_halt)(libusb_device_handle *dev_handle, unsigned char endpoint) = NULL;
extern "C" int libusb_clear_halt(libusb_device_handle *dev_handle, unsigned char endpoint)
{
	SETUP(libusb_clear_halt);

	msg(C_BRIGHT_CYAN, "[libusb_clear_halt:PRE]  dev_handle=%p endpoint=0x%02X\n",
		dev_handle, endpoint);

	int retval = REAL__libusb_clear_halt(dev_handle, endpoint);

	msg(C_BRIGHT_CYAN, "[libusb_clear_halt:POST] retval={ %d %s \"%s\" }\n",
		retval, libusb_error_name(retval), libusb_strerror(retval));

	return retval;
}


static int (*REAL__libusb_bulk_transfer)(libusb_device_handle *dev_handle, unsigned char endpoint, unsigned char *data, int length, int *actual_length, unsigned int timeout) = NULL;
extern "C" int libusb_bulk_transfer(libusb_device_handle *dev_handle, unsigned char endpoint, unsigned char *data, int length, int *actual_length, unsigned int timeout)
{
	SETUP(libusb_bulk_transfer);

	msg(C_BRIGHT_MAGENTA, "[libusb_bulk_transfer:PRE]  dev_handle=%p endpoint=0x%02X data=%p length=0x%X timeout=%u\n",
		dev_handle, endpoint, data, length, timeout);

	int retval = REAL__libusb_bulk_transfer(dev_handle, endpoint, data, length, actual_length, timeout);

	msg(C_BRIGHT_MAGENTA, "[libusb_bulk_transfer:POST] retval={ %d %s \"%s\" } *actual_length=0x%X\n",
		retval, libusb_error_name(retval), libusb_strerror(retval), *actual_length);

	return retval;
}


static void transfer_flags_str(struct libusb_transfer *transfer, char *buf, size_t size)
{
	if (transfer->flags == 0) {
		strlcpy(buf, "<none>", size);
		return;
	}

	buf[0] = '\x00';
	int num = 0;

	if ((transfer->flags & LIBUSB_TRANSFER_SHORT_NOT_OK) != 0) {
		if (num++ != 0) strlcat(buf, "|", size);
		strlcat(buf, "SHORT_NOT_OK", size);
	}
	if ((transfer->flags & LIBUSB_TRANSFER_FREE_BUFFER) != 0) {
		if (num++ != 0) strlcat(buf, "|", size);
		strlcat(buf, "FREE_BUFFER", size);
	}
	if ((transfer->flags & LIBUSB_TRANSFER_FREE_TRANSFER) != 0) {
		if (num++ != 0) strlcat(buf, "|", size);
		strlcat(buf, "FREE_TRANSFER", size);
	}
	if ((transfer->flags & LIBUSB_TRANSFER_ADD_ZERO_PACKET) != 0) {
		if (num++ != 0) strlcat(buf, "|", size);
		strlcat(buf, "ADD_ZERO_PACKET", size);
	}
}

#define TRANSFER_TYPE_STR_HELPER(type) \
	case LIBUSB_TRANSFER_TYPE_##type: strlcpy(buf, #type, size); break
static void transfer_type_str(struct libusb_transfer *transfer, char *buf, size_t size)
{
	switch (transfer->type) {
	TRANSFER_TYPE_STR_HELPER(CONTROL);
	TRANSFER_TYPE_STR_HELPER(ISOCHRONOUS);
	TRANSFER_TYPE_STR_HELPER(BULK);
	TRANSFER_TYPE_STR_HELPER(INTERRUPT);
	TRANSFER_TYPE_STR_HELPER(BULK_STREAM);
	default: strlcat(buf, "???", size); break;
	}
}

#define TRANSFER_STATUS_STR_HELPER(status) \
	case LIBUSB_TRANSFER_##status: strlcpy(buf, #status, size); break
static void transfer_status_str(struct libusb_transfer *transfer, char *buf, size_t size)
{
	switch (transfer->status) {
	TRANSFER_STATUS_STR_HELPER(COMPLETED);
	TRANSFER_STATUS_STR_HELPER(ERROR);
	TRANSFER_STATUS_STR_HELPER(TIMED_OUT);
	TRANSFER_STATUS_STR_HELPER(CANCELLED);
	TRANSFER_STATUS_STR_HELPER(STALL);
	TRANSFER_STATUS_STR_HELPER(NO_DEVICE);
	TRANSFER_STATUS_STR_HELPER(OVERFLOW);
	default: strlcat(buf, "???", size); break;
	}
}


// I'm generally worried about these various asynchronous things being called
// from different threads, so I'll use this BIG FAT MUTEX to allay those fears
static std::recursive_mutex big_fat_mutex;


static std::map<struct libusb_transfer *, libusb_transfer_cb_fn> callbacks;

static void xfer_cbfunc_shim(struct libusb_transfer *transfer)
{
	// uhhhhh I really hope that the callback isn't called internally via
	// libusb_submit_transfer or libusb_cancel_transfer, because that will make
	// this deadlock...
	// AHA! this is what they invented std::recursive_mutex for!
	std::lock_guard<std::recursive_mutex> protector(big_fat_mutex);

	// if this lookup fails, it will throw std::out_of_range
	libusb_transfer_cb_fn real_cbfunc = callbacks.at(transfer);

	{
		char buf_flags [1024]; transfer_flags_str (transfer, buf_flags,  sizeof(buf_flags));
		char buf_type  [1024]; transfer_type_str  (transfer, buf_type,   sizeof(buf_type));
		char buf_status[1024]; transfer_status_str(transfer, buf_status, sizeof(buf_status));
		msg(C_BRIGHT_YELLOW, "[libusb_transfer->callback:PRE(%p)]   transfer=%p { dev_handle=%p flags={ 0x%02X %s } endpoint=0x%02X type={ 0x%X %s } timeout=%u status={ 0x%X %s } length=%d actual_length=%d user_data=%p buffer=%p }\n",
			real_cbfunc, transfer, transfer->dev_handle, transfer->flags, buf_flags, transfer->endpoint, transfer->type, buf_type, transfer->timeout, transfer->status, buf_status, transfer->length, transfer->actual_length, transfer->user_data, transfer->buffer);
	}

	(*real_cbfunc)(transfer);

	{
		char buf_flags [1024]; transfer_flags_str (transfer, buf_flags,  sizeof(buf_flags));
		char buf_type  [1024]; transfer_type_str  (transfer, buf_type,   sizeof(buf_type));
		char buf_status[1024]; transfer_status_str(transfer, buf_status, sizeof(buf_status));
		msg(C_BRIGHT_YELLOW, "[libusb_transfer->callback:POST(%p)] transfer=%p { dev_handle=%p flags={ 0x%02X %s } endpoint=0x%02X type={ 0x%X %s } timeout=%u status={ 0x%X %s } length=%d actual_length=%d user_data=%p buffer=%p }\n",
			real_cbfunc, transfer, transfer->dev_handle, transfer->flags, buf_flags, transfer->endpoint, transfer->type, buf_type, transfer->timeout, transfer->status, buf_status, transfer->length, transfer->actual_length, transfer->user_data, transfer->buffer);
	}
}


static int (*REAL__libusb_submit_transfer)(struct libusb_transfer *transfer) = NULL;
extern "C" int libusb_submit_transfer(struct libusb_transfer *transfer)
{
	SETUP(libusb_submit_transfer);

	std::lock_guard<std::recursive_mutex> protector(big_fat_mutex);

	{
		char buf_flags [1024]; transfer_flags_str (transfer, buf_flags,  sizeof(buf_flags));
		char buf_type  [1024]; transfer_type_str  (transfer, buf_type,   sizeof(buf_type));
		char buf_status[1024]; transfer_status_str(transfer, buf_status, sizeof(buf_status));
		msg(C_BRIGHT_GREEN, "[libusb_submit_transfer:PRE]  transfer=%p { dev_handle=%p flags={ 0x%02X %s } endpoint=0x%02X type={ 0x%X %s } timeout=%u status={ 0x%X %s } length=%d actual_length=%d callback=%p user_data=%p buffer=%p }\n",
			transfer, transfer->dev_handle, transfer->flags, buf_flags, transfer->endpoint, transfer->type, buf_type, transfer->timeout, transfer->status, buf_status, transfer->length, transfer->actual_length, transfer->callback, transfer->user_data, transfer->buffer);
	}

	callbacks.emplace(transfer, transfer->callback);
	transfer->callback = &xfer_cbfunc_shim;

	int retval = REAL__libusb_submit_transfer(transfer);

	{
		char buf_flags [1024]; transfer_flags_str (transfer, buf_flags,  sizeof(buf_flags));
		char buf_type  [1024]; transfer_type_str  (transfer, buf_type,   sizeof(buf_type));
		char buf_status[1024]; transfer_status_str(transfer, buf_status, sizeof(buf_status));
		msg(C_BRIGHT_GREEN, "[libusb_submit_transfer:POST] transfer=%p { dev_handle=%p flags={ 0x%02X %s } endpoint=0x%02X type={ 0x%X %s } timeout=%u status={ 0x%X %s } length=%d actual_length=%d callback=%p user_data=%p buffer=%p } retval={ %d %s \"%s\" }\n",
			transfer, transfer->dev_handle, transfer->flags, buf_flags, transfer->endpoint, transfer->type, buf_type, transfer->timeout, transfer->status, buf_status, transfer->length, transfer->actual_length, transfer->callback, transfer->user_data, transfer->buffer, retval, libusb_error_name(retval), libusb_strerror(retval));
	}

	return retval;
}


static int (*REAL__libusb_cancel_transfer)(struct libusb_transfer *transfer) = NULL;
extern "C" int libusb_cancel_transfer(struct libusb_transfer *transfer)
{
	SETUP(libusb_cancel_transfer);

	std::lock_guard<std::recursive_mutex> protector(big_fat_mutex);

	{
		char buf_flags [1024]; transfer_flags_str (transfer, buf_flags,  sizeof(buf_flags));
		char buf_type  [1024]; transfer_type_str  (transfer, buf_type,   sizeof(buf_type));
		char buf_status[1024]; transfer_status_str(transfer, buf_status, sizeof(buf_status));
		msg(C_BRIGHT_RED, "[libusb_cancel_transfer:PRE]  transfer=%p { dev_handle=%p flags={ 0x%02X %s } endpoint=0x%02X type={ 0x%X %s } timeout=%u status={ 0x%X %s } length=%d actual_length=%d callback=%p user_data=%p buffer=%p }\n",
			transfer, transfer->dev_handle, transfer->flags, buf_flags, transfer->endpoint, transfer->type, buf_type, transfer->timeout, transfer->status, buf_status, transfer->length, transfer->actual_length, transfer->callback, transfer->user_data, transfer->buffer);
	}

	int retval = REAL__libusb_cancel_transfer(transfer);

	{
		char buf_flags [1024]; transfer_flags_str (transfer, buf_flags,  sizeof(buf_flags));
		char buf_type  [1024]; transfer_type_str  (transfer, buf_type,   sizeof(buf_type));
		char buf_status[1024]; transfer_status_str(transfer, buf_status, sizeof(buf_status));
		msg(C_BRIGHT_RED, "[libusb_cancel_transfer:POST] transfer=%p { dev_handle=%p flags={ 0x%02X %s } endpoint=0x%02X type={ 0x%X %s } timeout=%u status={ 0x%X %s } length=%d actual_length=%d callback=%p user_data=%p buffer=%p } retval={ %d %s \"%s\" }\n",
			transfer, transfer->dev_handle, transfer->flags, buf_flags, transfer->endpoint, transfer->type, buf_type, transfer->timeout, transfer->status, buf_status, transfer->length, transfer->actual_length, transfer->callback, transfer->user_data, transfer->buffer, retval, libusb_error_name(retval), libusb_strerror(retval));
	}

	return retval;
}
