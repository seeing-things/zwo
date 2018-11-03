#ver = debug
ver = release
platform = x64

CC = g++
OPENCV = -lopencv_core -lopencv_highgui -lopencv_imgproc#$(shell pkg-config --cflags opencv) $(shell pkg-config --libs opencv) -I/usr/include/opencv2
USB = -I../libusb/include  -L../libusb/$(platform) -lusb-1.0



LIBSPATH = -L../linux_sdk/lib/$(platform) -I../linux_sdk/include -I./include



DEFS   = -D_LIN -D_GNU_SOURCE
CFLAGS = -std=c++17 -Wall -g $(DEFS) $(COMMON) $(LIBSPATH) -lpthread -DGLIBC_20

ifeq ($(ver), debug)
	DEFS   += -D_DEBUG
	CFLAGS += -O0
else
	CFLAGS += -O3
endif


ifeq ($(platform), x64)
	CFLAGS += -m64
	CFLAGS += -lrt
endif


all: bin/capture bin/write_benchmark

bin/capture: src/Frame.cpp src/SERFile.cpp src/capture.cpp
	mkdir -p bin
	$(CC) $^ -o $@ $(CFLAGS) $(OPENCV) -lASICamera2

bin/write_benchmark: write_benchmark.cpp
	mkdir -p bin
	$(CC) $< -o $@ $(CFLAGS)

clean:
	rm -f bin/*

#pkg-config libusb-1.0 --cflags --libs
#pkg-config opencv --cflags --libs

