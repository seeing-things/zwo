#!/bin/bash

LD_LIBRARY_PATH="$LD_LIBRARY_PATH:." LD_PRELOAD="../libusb_wrapper.so" ./example "$@"
