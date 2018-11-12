#include "CCameraFX3.h"



static void callbackUSBTransferComplete(libusb_transfer *transfer)
{
	// TODO
}


void CCameraFX3::initAsyncXfer(int total_len, int num_xfers, int xfer_len, char endpoint, uint8_t *xfer_buf)
{
	// TODO
}


bool CCameraFX3::startAsyncXfer(unsigned int timeout_ms_1, unsigned int timeout_ms_2, int *a4_out, bool *a5_unused, int a6)
{
	// TODO
}


void CCameraFX3::releaseAsyncXfer()
{
	// TODO
}
