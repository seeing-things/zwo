SHELL:=/bin/bash


CXXFLAGS:=-std=c++17 -fno-exceptions -fno-strict-aliasing -D_GLIBCXX_USE_CXX11_ABI=0 -fdiagnostics-color=always -Wall -Wno-unused-variable -Wno-unused-function -O1 -fuse-linker-plugin -fvisibility=hidden -fvisibility-inlines-hidden -g3 -gdwarf-4 -fvar-tracking-assignments -fno-omit-frame-pointer -fuse-ld=gold -shared -fPIC -fno-plt -fno-gnu-unique -rdynamic -Wl,--no-gc-sections -Wl,--no-undefined -Wl,-z,defs -lbsd -ldl -lelf -lusb-1.0


# omit leading zeroes or else face the wrath of octal interpretation
ZWO_VERSION_MAJOR   :=0
ZWO_VERSION_MINOR   :=7
ZWO_VERSION_REVISION:=503

ZWO_VERSION_STRING:=$(shell printf "%u.%u.%04u"     $(ZWO_VERSION_MAJOR) $(ZWO_VERSION_MINOR) $(ZWO_VERSION_REVISION))
ZWO_VERSION       :=$(shell printf "0x%02u%02u%04u" $(ZWO_VERSION_MAJOR) $(ZWO_VERSION_MINOR) $(ZWO_VERSION_REVISION))

CXXFLAGS+=-DZWO_VERSION_MAJOR=$(ZWO_VERSION_MAJOR)
CXXFLAGS+=-DZWO_VERSION_MINOR=$(ZWO_VERSION_MINOR)
CXXFLAGS+=-DZWO_VERSION_REVISION=$(ZWO_VERSION_REVISION)
CXXFLAGS+=-DZWO_VERSION_STRING=\"$(ZWO_VERSION_STRING)\"
CXXFLAGS+=-DZWO_VERSION=$(ZWO_VERSION)U


.PHONY: all clean

all: libzwo_fixer.so

clean:
	-rm -f libzwo_fixer.so

libzwo_fixer.so: zwo_fixer.cpp zwo_fixer.hpp Makefile
	g++ $(CXXFLAGS) -o $@ $<
