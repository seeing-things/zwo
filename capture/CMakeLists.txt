cmake_minimum_required(VERSION 3.16)
project(
    capture
    VERSION 0.1.0
    LANGUAGES CXX
)

find_package(OpenCV REQUIRED COMPONENTS core highgui imgproc)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(spdlog REQUIRED)

# Find packages using pkg-config (only use for packages that `find_package` can't handle).
find_package(PkgConfig REQUIRED)
pkg_search_module(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
pkg_search_module(LIBBSD REQUIRED IMPORTED_TARGET libbsd)

add_subdirectory(src)
