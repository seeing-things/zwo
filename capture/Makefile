#VERSION = debug
 VERSION = release

#PLATFORM = x86
 PLATFORM = x64
#PLATFORM = armv5
#PLATFORM = armv6
#PLATFORM = armv7
#PLATFORM = armv8


CC = g++

OPENCV = -lopencv_core -lopencv_highgui -lopencv_imgproc#$(shell pkg-config --cflags opencv) $(shell pkg-config --libs opencv) -I/usr/include/opencv2
USB = -I../libusb/include  -L../libusb/$(PLATFORM) -lusb-1.0

# "hack" for compat with Arch Linux's opencv include path, since we aren't using pkg-config in here for some idiotic reason
OPENCV += -I/usr/include/opencv4

LIBS = -lrt -lbsd


ASI_VERSION = 1.18

ASI_BASEPATH = ../$(ASI_VERSION)/linux_sdk

ASI_INCPATH  = $(ASI_BASEPATH)/include
ASI_LIBPATH  = $(ASI_BASEPATH)/lib/$(PLATFORM)

ASI_CFLAGS  = -I$(ASI_INCPATH)
ASI_LDFLAGS = -L$(ASI_LIBPATH) -l:libASICamera2.so.$(ASI_VERSION)


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

ifeq ($(PLATFORM), x86)
	CFLAGS += -m32
endif

ifeq ($(PLATFORM), x64)
	CFLAGS += -m64
endif

ifeq ($(PLATFORM), armv5)
#	CC      = arm-none-linux-gnueabi-g++
	CFLAGS += -march=armv5
endif

ifeq ($(PLATFORM), armv6)
#	CC      = arm-linux-gnueabi-g++
	CFLAGS += -march=armv6
endif

ifeq ($(PLATFORM), armv7)
#	CC      = arm-linux-gnueabihf-g++
	CFLAGS += -march=armv7 -mcpu=cortex-m3 -mthumb
endif

ifeq ($(PLATFORM), armv8)
#	CC = aarch64-linux-gnu-g++
endif


all: bin/capture bin/write_benchmark

bin/capture: src/Frame.cpp src/SERFile.cpp src/agc.cpp src/disk.cpp src/preview.cpp src/camera.cpp src/capture.cpp
	make -C $(ZWOFIXER_PATH) all
	mkdir -p bin
	$(CC) $^ -o $@ $(CFLAGS) $(LIBS) $(OPENCV) $(ASI_CFLAGS) $(ASI_LDFLAGS) $(ZWOFIXER_CFLAGS) $(ZWOFIXER_LDFLAGS)

bin/write_benchmark: write_benchmark.cpp
	mkdir -p bin
	$(CC) $< -o $@ $(CFLAGS) $(LIBS)

clean:
	make -C $(ZWOFIXER_PATH) clean
	rm -f bin/*

#pkg-config libusb-1.0 --cflags --libs
#pkg-config opencv --cflags --libs

