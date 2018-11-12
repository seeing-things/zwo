#include "CCameraS178MC.h"


constexpr int MiB = (1 << 20);


// TODO: look through the Windows version of this function and #ifdef the divergent parts

static void *WorkingFunc(void *arg)
{
	auto this = reinterpret_cast<CCameraS178MC *>(arg);
	
	int v46 = 0;
	
	static uint8_t old_autoFPS = this->fpsperc_auto;
	
	int tickcount_1 = GetTickCount();
	int tickcount_2 = GetTickCount();
	
	this->ResetDevice();
	Sleep(50);
	
	this->SendCMD(USB_REQ_ZWO_STOP_SENSOR_STREAMING);
	this->StopSensorStreaming();
	
	DbgPrint("working thread begin!\n");
	
	this->field_524 = 0; // 32-bit integer
	
	this->cirbuf_ptr->ResetCirBuff();
	
	int image_size = this->GetRealImageSize();
	if (image_size < 0) image_size += (MiB - 1);
	
	int image_size_MiB = image_size / MiB;
	if ((image_size & (MiB - 1)) != 0) ++image_size_MiB;
	
	if (!this->field_95) { // bool
		this->field_70C = 100'000; // 32-bit integer
		this->StartAutoControlThr();
	}
	
	this->SendCMD(USB_REQ_ZWO_START_SENSOR_STREAMING);
	this->StartSensorStreaming();
	
	this->ResetEndPoint(0x81);
	
	this->initAsyncXfer(image_size, image_size_MiB, MiB, 0x81, this->field_538);
	
	
	
	// TODO vvv
	
	
	
	
	// TODO ^^^
	
	return nullptr;
}
