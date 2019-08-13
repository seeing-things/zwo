#VERSION = debug
 VERSION = release

PLATFORM = x64


CC = g++

OPENCV = -lopencv_core -lopencv_highgui -lopencv_imgproc#$(shell pkg-config --cflags opencv) $(shell pkg-config --libs opencv) -I/usr/include/opencv2
USB = -I../libusb/include  -L../libusb/$(PLATFORM) -lusb-1.0


# "hack" for compat with Arch Linux's opencv include path, since we aren't using pkg-config in here for some idiotic reason
OPENCV += -I/usr/include/opencv4


ASI_VERSION = 1.14.0715

ASI_BASEPATH = ../$(ASI_VERSION)/linux_sdk

ASI_INCPATH  = $(ASI_BASEPATH)/include
ASI_LIBPATH  = $(ASI_BASEPATH)/lib/$(PLATFORM)

ASI_CFLAGS  = -I$(ASI_INCPATH)
ASI_LDFLAGS = -Wl,-rpath,$(ASI_LIBPATH) -Wl,-rpath,$(shell realpath $(ASI_LIBPATH)) -L$(ASI_LIBPATH) -l:libASICamera2.so.$(ASI_VERSION)


ZWOFIXER_PATH=../zwo_fixer

ZWOFIXER_CFLAGS  = -I$(ZWOFIXER_PATH)
ZWOFIXER_LDFLAGS = -Wl,-rpath,$(ZWOFIXER_PATH) -Wl,-rpath,$(shell realpath $(ZWOFIXER_PATH)) -L$(ZWOFIXER_PATH) -l:libzwo_fixer.so


DEFS   = -D_LIN -D_GNU_SOURCE
CFLAGS = -std=c++17 -Wall -g $(DEFS) -I./include -pthread -DGLIBC_20

ifeq ($(VERSION), debug)
	DEFS   += -D_DEBUG
	CFLAGS += -O0
else
	CFLAGS += -O3
endif

ifeq ($(PLATFORM), x64)
	CFLAGS += -m64
	CFLAGS += -lrt
endif


all: bin/capture bin/write_benchmark

bin/capture: src/Frame.cpp src/SERFile.cpp src/agc.cpp src/disk.cpp src/preview.cpp src/camera.cpp src/capture.cpp
	make -C $(ZWOFIXER_PATH) all
	mkdir -p bin
	$(CC) $^ -o $@ $(CFLAGS) $(OPENCV) $(ASI_CFLAGS) $(ASI_LDFLAGS) $(ZWOFIXER_CFLAGS) $(ZWOFIXER_LDFLAGS)

bin/write_benchmark: write_benchmark.cpp
	mkdir -p bin
	$(CC) $< -o $@ $(CFLAGS)

clean:
	make -C $(ZWOFIXER_PATH) clean
	rm -f bin/*

#pkg-config libusb-1.0 --cflags --libs
#pkg-config opencv --cflags --libs

