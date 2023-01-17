# Set build type to Release by default. This adds compiler optimization flags.
set(CMAKE_BUILD_TYPE
    "Release"
    CACHE STRING "")

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

# Find packages using pkg-config (only use for packages that `find_package` can't handle). Each
# `pkg_search_module()` will create a target named PkgConfig::<prefix> where <prefix> is the first
# argument.
find_package(PkgConfig REQUIRED)
pkg_search_module(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
pkg_search_module(LIBBSD REQUIRED IMPORTED_TARGET libbsd)

add_subdirectory(src)
