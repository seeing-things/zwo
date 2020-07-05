#include "internal.hpp"
#include "zwo_fixer.hpp"


// PLT hooks ===================================================================


/*
- hook CCameraFX3::startAsyncXfer so we can figure out what the hell the two timeout_ms values actually are being calculated as...

when -EOVERFLOW happens:
- the callbackUSBTransferComplete callback function should (AFAIK) get transfer->status == LIBUSB_TRANSFER_OVERFLOW (6)
  - maybe add a hook that prints a useful error message when it gets that status value or something
    - can't do this directly with a PLT hook; instead, just make a wrapper callback func around
      callbackUSBTransferComplete and use a PLT hook for libusb_submit_transfer to substitute our func ptr for the
      xfer->callback member
*/


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
	GetAddr(".got.plt:libusb_cancel_transfer"),
	+[](libusb_transfer *transfer) -> int {
		int retval = libusb_cancel_transfer(transfer);
		
		// falsify some variables so that CCameraFX3::startAsyncXfer will think that
		// callbackUSBTransferComplete *did* get called but had some kind of problem
		if (retval == LIBUSB_ERROR_NOT_FOUND) {
			Msg(Color::GREEN, "PLTHook(libusb_cancel_transfer): got LIBUSB_ERROR_NOT_FOUND; fixing broken code\n");
			lin_XferLen        = -1;
			lin_XferCallbacked = true;
		} else {
//			Msg(Color::YELLOW, "PLTHook(libusb_cancel_transfer): got %s; ignoring\n", libusb_strerror(retval));
		}
		
		return retval;
	}
);

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
	atexit(&ZWOFixerExit);
	
	bool ok = IsLibASILoadedAndSupported();
	Msg((ok ? Color::GREEN : Color::RED), "ZWOFixerInit: %s\n", (ok ? "OK" : "FAIL"));
	return ok;
}

// =============================================================================
