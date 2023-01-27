add_executable(capture agc.cpp camera.cpp capture.cpp disk.cpp Frame.cpp preview.cpp SERFile.cpp)

target_compile_features(capture PRIVATE cxx_std_17)
# setting compiler options directly isn't portable since they're compiler-specific, but I never
# promised this would work with anything other than gcc
target_compile_options(capture PRIVATE -Wall)
set_target_properties(capture PROPERTIES CXX_EXTENSIONS OFF)
set_target_properties(capture PROPERTIES RUNTIME_OUTPUT_DIRECTORY ..)

target_include_directories(capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_include_directories(capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../1.18/linux_sdk/include)

target_link_directories(capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../1.18/linux_sdk/lib/x64)

target_link_libraries(capture PRIVATE PkgConfig::LIBUSB)
target_link_libraries(capture PRIVATE PkgConfig::LIBBSD)
target_link_libraries(capture PRIVATE ${OpenCV_LIBS})
target_link_libraries(capture PRIVATE Threads::Threads)
target_link_libraries(capture PRIVATE spdlog::spdlog)
target_link_libraries(capture PRIVATE libASICamera2.so.1.18)
