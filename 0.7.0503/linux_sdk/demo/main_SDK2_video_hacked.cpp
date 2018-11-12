#include "stdio.h"
#include "opencv2/highgui/highgui_c.h"
#include "pthread.h"
#include "ASICamera2.h"
#include <sys/time.h>
#include <time.h>




#define  MAX_CONTROL 7


void cvText(IplImage* img, const char* text, int x, int y)
{
	CvFont font;

	double hscale = 0.6;
	double vscale = 0.6;
	int linewidth = 2;
	cvInitFont(&font,CV_FONT_HERSHEY_SIMPLEX | CV_FONT_ITALIC,hscale,vscale,0,linewidth);

	CvScalar textColor =cvScalar(255,255,255);
	CvPoint textPos =cvPoint(x, y);

	cvPutText(img, text, textPos, &font,textColor);
}
extern unsigned long GetTickCount();

int bDisplay = 0;
int bMain = 1;
int bChangeFormat = 0;
ASI_CAMERA_INFO CamInfo;
enum CHANGE{
	change_imagetype = 0,
	change_bin,
	change_size_bigger,
	change_size_smaller
};
CHANGE change;
void* Display(void* params)
{
	IplImage *pImg = (IplImage *)params;
	cvNamedWindow("video", 1);
	while(bDisplay)
	{
		cvShowImage("video", pImg);

		char c=cvWaitKey(1);
		switch(c)
		{
			case 27://esc
			bDisplay = false;
			bMain = false;
			goto END;

			case 'i'://space
			bChangeFormat = true;
			change = change_imagetype;
			break;

			case 'b'://space
			bChangeFormat = true;
			change = change_bin;
			break;

			case 'w'://space
			bChangeFormat = true;
			change = change_size_smaller;
			break;

			case 's'://space
			bChangeFormat = true;
			change = change_size_bigger;
			break;
		}
	}
END:
	cvDestroyWindow("video");
	printf("Display thread over\n");
	ASIStopVideoCapture(CamInfo.CameraID);
	return (void*)0;
}
int  main()
{
	int width;
	char* bayer[] = {"RG","BG","GR","GB"};
	char* controls[MAX_CONTROL] = {"Exposure", "Gain", "Gamma", "WB_R", "WB_B", "Brightness", "USB Traffic"};

	int height;
	int i;
	char c;
	bool bresult;

	int time1,time2;
	int count=0;

	char buf[128]={0};

	int CamIndex=0;


	IplImage *pRgb;


	int numDevices = ASIGetNumOfConnectedCameras();
	if(numDevices <= 0)
	{
		printf("no camera connected, press any key to exit\n");
		getchar();
		return -1;
	}
	else
		printf("attached cameras:\n");

	for(i = 0; i < numDevices; i++)
	{
		ASIGetCameraProperty(&CamInfo, i);
		printf("%d %s\n",i, CamInfo.Name);
	}

	printf("\nselect one to privew\n");
	scanf("%d", &CamIndex);


	ASIGetCameraProperty(&CamInfo, CamIndex);
	bresult = ASIOpenCamera(CamInfo.CameraID);
	bresult += ASIInitCamera(CamInfo.CameraID);
	if(bresult)
	{
		printf("OpenCamera error,are you root?,press any key to exit\n");
		getchar();
		return -1;
	}

	printf("%s information\n",CamInfo.Name);
	int iMaxWidth, iMaxHeight;
	iMaxWidth = CamInfo.MaxWidth;
	iMaxHeight =  CamInfo.MaxHeight;
	printf("resolution:%dX%d\n", iMaxWidth, iMaxHeight);
	if(CamInfo.IsColorCam)
		printf("Color Camera: bayer pattern:%s\n",bayer[CamInfo.BayerPattern]);
	else
		printf("Mono camera\n");

	int ctrlnum;
	ASIGetNumOfControls(CamInfo.CameraID, &ctrlnum);
	ASI_CONTROL_CAPS ctrlcap;
	for( i = 0; i < ctrlnum; i++)
	{
		ASIGetControlCaps(CamInfo.CameraID, i,&ctrlcap);

		printf("%s\n", ctrlcap.Name);

	}

	// printf("\nPlease input the <width height bin image_type> with one space, ie. 640 480 2 0. use max resolution if input is 0. Press ESC when video window is focused to quit capture\n");
	int bin = 1, Image_type;
	// scanf("%d %d %d %d", &width, &height, &bin, &Image_type);
	// if(width == 0 || height == 0)
	// {
	// 	width = iMaxWidth;
	// 	height = iMaxHeight;
	// }

	width = 3096;
	height = 2080;
	Image_type = 0;


	long lVal;
	ASI_BOOL bAuto;
	ASIGetControlValue(CamInfo.CameraID, ASI_TEMPERATURE, &lVal, &bAuto);
	printf("sensor temperature:%.1f\n", lVal/10.0);



	// while(ASISetROIFormat(CamInfo.CameraID, width, height, bin, (ASI_IMG_TYPE)Image_type))//IMG_RAW8
	// {
	// 	printf("Set format error, please check the width and height\n ASI120's data size(width*height) must be integer multiple of 1024\n");
	// 	printf("Please input the width and height again, ie. 640 480\n");
	// 	scanf("%d %d %d %d", &width, &height, &bin, &Image_type);
	// }
	// printf("\nset image format %d %d %d %d success, start privew, press ESC to stop \n", width, height, bin, Image_type);

	if (ASISetROIFormat(CamInfo.CameraID, width, height, bin, (ASI_IMG_TYPE)Image_type))
	{
		printf("Problem setting the ROI format\n");
		exit(1);
	}

	if(Image_type == ASI_IMG_RAW16)
		pRgb=cvCreateImage(cvSize(width,height), IPL_DEPTH_16U, 1);
	else if(Image_type == ASI_IMG_RGB24)
		pRgb=cvCreateImage(cvSize(width,height), IPL_DEPTH_8U, 3);
	else
		pRgb=cvCreateImage(cvSize(width,height), IPL_DEPTH_8U, 1);

	printf("How high of a speed can you handle?? Enter it here: ");
	int overload = 0;
	scanf("%d", &overload);
	printf("You asked for an overload of: %d\n", overload);

	int exp_ms;
	printf("Please input exposure time(ms)\n");
	scanf("%d", &exp_ms);
	ASISetControlValue(CamInfo.CameraID,ASI_EXPOSURE, exp_ms*1000, ASI_FALSE);
	ASISetControlValue(CamInfo.CameraID,ASI_GAIN,0, ASI_FALSE);
	ASISetControlValue(CamInfo.CameraID,ASI_BANDWIDTHOVERLOAD, overload, ASI_FALSE); //low transfer speed
	printf("Uh oh: High speed mode activated!!!\n");
	ASISetControlValue(CamInfo.CameraID,ASI_HIGH_SPEED_MODE, 1, ASI_FALSE);
	ASISetControlValue(CamInfo.CameraID,ASI_WB_B, 90, ASI_FALSE);
 	ASISetControlValue(CamInfo.CameraID,ASI_WB_R, 48, ASI_FALSE);

	ASIStartVideoCapture(CamInfo.CameraID); //start privew




	bDisplay = 1;
#ifdef _LIN
	//pthread_t thread_display;
	//pthread_create(&thread_display, NULL, Display, (void*)pRgb);
#elif defined _WINDOWS
	HANDLE thread_setgainexp;
	thread_setgainexp = (HANDLE)_beginthread(Display,  NULL, (void*)pRgb);
#endif

	time1 = GetTickCount();
	int iStrLen = 0, iTextX = 40, iTextY = 60;
	void* retval;

	unsigned char *data = (unsigned char*)malloc(10000000);

	int iDropFrmae;
	while(bMain)
	{


		int retval = ASIGetVideoData(CamInfo.CameraID, data, pRgb->imageSize, exp_ms<=100?200:exp_ms*2);
		if (retval == ASI_SUCCESS)
		{
			count++;
		}
		else
		{
			printf("GetVideoData failed with error code %d\n", retval);
		}


		time2 = GetTickCount();



		if(time2-time1 > 1000 )
		{
			ASIGetDroppedFrames(CamInfo.CameraID, &iDropFrmae);
			sprintf(buf, "fps:%d dropped frames:%lu ImageType:%d",count, iDropFrmae, (int)Image_type);

			count = 0;
			time1=GetTickCount();
			printf(buf);
			printf("\n");

		}

		#if 0
		if(Image_type != ASI_IMG_RGB24 && Image_type != ASI_IMG_RAW16)
		{
			iStrLen = strlen(buf);
			CvRect rect = cvRect(iTextX, iTextY - 15, iStrLen* 11, 20);
			cvSetImageROI(pRgb , rect);
			cvSet(pRgb, CV_RGB(180, 180, 180));
			cvResetImageROI(pRgb);
		}
		cvText(pRgb, buf, iTextX,iTextY );
		#endif

		if(bChangeFormat)
		{
			bChangeFormat = 0;
			bDisplay = false;
			//pthread_join(thread_display, &retval);
			cvReleaseImage(&pRgb);
			ASIStopVideoCapture(CamInfo.CameraID);

			switch(change)
			{
				 case change_imagetype:
					Image_type++;
					if(Image_type > 3)
						Image_type = 0;

					break;
				case change_bin:
					if(bin == 1)
					{
						bin = 2;
						width/=2;
						height/=2;
					}
					else
					{
						bin = 1;
						width*=2;
						height*=2;
					}
					break;
				case change_size_smaller:
					if(width > 320 && height > 240)
					{
						width/= 2;
						height/= 2;
					}
					break;

				case change_size_bigger:

					if(width*2*bin <= iMaxWidth && height*2*bin <= iMaxHeight)
					{
						width*= 2;
						height*= 2;
					}
					break;
			}
			ASISetROIFormat(CamInfo.CameraID, width, height, bin, (ASI_IMG_TYPE)Image_type);
			if(Image_type == ASI_IMG_RAW16)
				pRgb=cvCreateImage(cvSize(width,height), IPL_DEPTH_16U, 1);
			else if(Image_type == ASI_IMG_RGB24)
				pRgb=cvCreateImage(cvSize(width,height), IPL_DEPTH_8U, 3);
			else
				pRgb=cvCreateImage(cvSize(width,height), IPL_DEPTH_8U, 1);
			bDisplay = 1;
			//pthread_create(&thread_display, NULL, Display, (void*)pRgb);
			ASIStartVideoCapture(CamInfo.CameraID); //start privew
		}
	}
END:

	if(bDisplay)
	{
		bDisplay = 0;
#ifdef _LIN
   		//pthread_join(thread_display, &retval);
#elif defined _WINDOWS
		Sleep(50);
#endif
	}

	ASIStopVideoCapture(CamInfo.CameraID);
	ASICloseCamera(CamInfo.CameraID);
	cvReleaseImage(&pRgb);
	printf("main function over\n");
	return 1;
}






