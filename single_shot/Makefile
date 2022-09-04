VERSION = debug
PLATFORM = x64
CC = g++

USB = -I../libusb/include  -L../libusb/$(PLATFORM) -lusb-1.0


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

ifeq ($(PLATFORM), x64)
	CFLAGS += -m64
endif


all: bin/single_shot

bin/single_shot: single_shot.cpp $(ZWOFIXER_PATH)/*.hpp $(ZWOFIXER_PATH)/*.cpp
	make -C $(ZWOFIXER_PATH) all
	mkdir -p bin
	$(CC) single_shot.cpp -o $@ $(CFLAGS) $(LIBS) $(ASI_CFLAGS) $(ASI_LDFLAGS) $(ZWOFIXER_CFLAGS) $(ZWOFIXER_LDFLAGS)

clean:
	make -C $(ZWOFIXER_PATH) clean
	rm -f bin/*

#pkg-config libusb-1.0 --cflags --libs
#pkg-config opencv --cflags --libs
