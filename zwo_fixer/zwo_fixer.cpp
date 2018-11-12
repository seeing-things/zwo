#include "zwo_fixer.hpp"


// PLT hooks ===================================================================

// proof-of-concept hook
// also potentially useful for doing the buf3_ptr libusb_dev_mem_alloc/libusb_dev_mem_free thing
// (though upon further consideration we'd want to do the buf3_ptr switcheroo somewhere else)
#if 0
PLTHook plthook__libusb_open(
	"libusb_open",
	libASICamera2_PLT__libusb_open,
	+[](libusb_device *dev, libusb_device_handle **handle) -> int {
		Msg(Color::MAGENTA, "PLTHook(libusb_open): entry: dev = 0x%016" PRIX64 " *handle = 0x%016" PRIX64 "\n",
			(uint64_t)dev, (uint64_t)(*handle));
		
		uint8_t dev_bus              = libusb_get_bus_number(dev);
		uint8_t dev_port             = libusb_get_port_number(dev);
		uint8_t dev_addr             = libusb_get_device_address(dev);
		int dev_speed                = libusb_get_device_speed(dev);
		int dev_max_packet_size_0x81 = libusb_get_max_packet_size(dev, 0x81);
		
		Msg(Color::MAGENTA, "PLTHook(libusb_open): pre: bus %d port %d addr %d speed %d maxpktsize %d\n",
			dev_bus, dev_port, dev_addr, dev_speed, dev_max_packet_size_0x81);
		
		int retval = libusb_open(dev, handle);
		
		Msg(Color::MAGENTA, "PLTHook(libusb_open): exit: retval = %d *handle = 0x%016" PRIX64 "\n",
			retval, (uint64_t)(*handle));
		
		return retval;
	}
);
#endif


// ZWO is too stupid to bother checking the return values of basically any of the libusb functions they call;
// and in particular, in CCameraFX3::startAsyncXfer, in the error case where their transfer callback function didn't
// get called, they call libusb_cancel_transfer BUT FAIL TO CHECK THE RETURN VALUE.
//
// The documentation for libusb_cancel_transfer says that the callback will be called with status
// LIBUSB_TRANSFER_CANCELLED; however it also clearly states that if the transfer is not in progress, already complete,
// or already cancelled, that libusb_cancel_transfer will return LIBUSB_ERROR_NOT_FOUND and no callback will occur.
//
// Well, rather than just check the return code, ZWO implemented a 500 ms timeout in the latter part of
// CCameraFX3::startAsyncXfer, which will cause the library to do nothing for half a second after a transfer failure of
// some kind occurs.
//
// This fixes that idiocy.
PLTHook plthook__libusb_cancel_transfer(
	"libusb_cancel_transfer",
	libASICamera2_PLT__libusb_cancel_transfer,
	+[](struct libusb_transfer *transfer) -> int {
		int retval = libusb_cancel_transfer(transfer);
		
		// falsify some variables so that CCameraFX3::startAsyncXfer will think that
		// callbackUSBTransferComplete *did* get called but had some kind of problem
		if (retval == LIBUSB_ERROR_NOT_FOUND) {
			Msg(Color::GREEN, "PLTHook(libusb_cancel_transfer): got LIBUSB_ERROR_NOT_FOUND; fixing broken code\n");
			*lin_XferLen        = -1;
			*lin_XferCallbacked = true;
		} else {
//			Msg(Color::YELLOW, "PLTHook(libusb_cancel_transfer): got %s; ignoring\n", libusb_strerror(retval));
		}
		
		return retval;
	}
);



/*

- hook CCameraFX3::startAsyncXfer so we can figure out what the hell the two timeout_ms values actually are being calculated as...

when -EOVERFLOW happens:
- the callbackUSBTransferComplete callback function should (AFAIK) get transfer->status == LIBUSB_TRANSFER_OVERFLOW (6)
  - maybe add a hook that prints a useful error message when it gets that status value or something
    - can't do this directly with a PLT hook; instead, just make a wrapper callback func around
      callbackUSBTransferComplete and use a PLT hook for libusb_submit_transfer to substitute our func ptr for the
      xfer->callback member

*/



// =============================================================================


// atexit handler ==============================================================

static void ZWOFixerExit()
{
	// ...
}

// =============================================================================


// Externally visible API ======================================================

// Call this BEFORE calling any ZWO API functions!
// Returns true if fixes were able to be applied successfully
extern "C" [[gnu::visibility("default")]]
bool ZWOFixerInit()
{
//	Msg(Color::WHITE, "ZWOFixerInit: installing PLT hook for libusb_open...\n");
//	plthook__libusb_open.Install();
//	Msg(Color::WHITE, "ZWOFixerInit: installed  PLT hook for libusb_open.\n");
	
#if 0
	void *dl_handle = nullptr;
	assert((dl_handle = dlopen("libASICamera2.so." ZWO_VERSION_STRING, RTLD_NOW | RTLD_NOLOAD)) != nullptr);
	// ...
	assert(dlclose(dl_handle) == 0);
#endif
	
	atexit(&ZWOFixerExit);
	
	Msg(Color::WHITE, "ZWOFixerInit: %s\n", (g_Fail ? "FAIL" : "OK"));
	return !g_Fail;
}

// =============================================================================



// libusb_open:
// via libusb_open_device_with_vid_pid_index
// via CCameraBase::OpenCamera
// via the vtable...

// libusb_close:
// via CCameraFX3::CloseDevice
// via CCameraBase::CloseCam
// via CCameraS178MC dtor (before CCameraBase dtor)


// unsigned char *libusb_dev_mem_alloc(libusb_device_handle *dev_handle, size_t length)
// - returns NULL on failure

// int libusb_dev_mem_free(libusb_device_handle *dev_handle, unsigned char *buffer, size_t length)
// - returns LIBUSB_SUCCESS on success
