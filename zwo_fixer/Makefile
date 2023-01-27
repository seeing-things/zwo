SHELL:=/bin/bash


CXXFLAGS:=-std=c++17 -fno-exceptions -fno-strict-aliasing -D_GLIBCXX_USE_CXX11_ABI=0 -fdiagnostics-color=always -Wall -Wno-unused-variable -Wno-unused-function -O1 -fuse-linker-plugin -fvisibility=hidden -fvisibility-inlines-hidden -g3 -fvar-tracking-assignments -fno-omit-frame-pointer -fuse-ld=gold -shared -fPIC -fno-plt -fno-gnu-unique -rdynamic -Wl,--no-gc-sections -Wl,--no-undefined -Wl,-z,defs -lbsd -ldl -lelf -lusb-1.0


.PHONY: all clean

all: libzwo_fixer.so

clean:
	-rm -f libzwo_fixer.so

libzwo_fixer.so: zwo_fixer.cpp zwo_fixer.hpp internal.hpp Makefile
	g++ $(CXXFLAGS) -o $@ $<
