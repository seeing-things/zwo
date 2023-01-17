# Set build type to Release by default. This adds compiler optimization flags.
set(CMAKE_BUILD_TYPE
    "Release"
    CACHE STRING "")

cmake_minimum_required(VERSION 3.16)
project(
    zwo_fixer
    VERSION 0.1.0
    LANGUAGES CXX
)

add_library(libzwo_fixer zwo_fixer.cpp)

target_compile_features(libzwo_fixer PRIVATE cxx_std_17)
# setting compiler options directly isn't portable since they're compiler-specific, but I never
# promised this would work with anything other than gcc
target_compile_options(libzwo_fixer PRIVATE -Wall)
set_target_properties(libzwo_fixer PROPERTIES CXX_EXTENSIONS OFF)

# Equivalent to passing -ldl to gcc
target_link_libraries(libzwo_fixer PRIVATE ${CMAKE_DL_LIBS})
