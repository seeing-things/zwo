## Build Dependencies

To compile the capture software, a C++ compiler is required. On Debian-based Linux distributions (e.g. Ubuntu), you will need the build-essential package for this.

The following libraries are required for building the `zwo_fixer` shim library (Debian package names in parentheses):
- librt (libc6-dev)
- libpthread (libc6-dev)
- libdl (libc6-dev)
- libbsd (libbsd-dev)
- libelf (libelf-dev)
- libusb-1.0 (libusb-1.0-0-dev)

The following libraries are required for building the `capture` program (Debian package names in parentheses):
- librt (libc6-dev)
- libpthread (libc6-dev)
- libusb-1.0 (libusb-1.0-0-dev)
- libopencv-core (libopencv-core-dev)
- libopencv-highui (libopencv-highui-dev)
- libopencv-imgproc (libopencv-imgproc-dev)
