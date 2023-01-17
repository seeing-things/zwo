add_executable(capture agc.cpp camera.cpp capture.cpp disk.cpp Frame.cpp preview.cpp SERFile.cpp)

target_compile_features(capture PRIVATE cxx_std_17)
set_target_properties(capture PROPERTIES CXX_EXTENSIONS OFF)

target_include_directories(capture PRIVATE ../include)
target_include_directories(capture PRIVATE ../../zwo_fixer)

target_link_directories(capture PRIVATE ../../zwo_fixer)

target_link_libraries(capture PRIVATE PkgConfig::LIBUSB)
target_link_libraries(capture PRIVATE PkgConfig::LIBBSD)
target_link_libraries(capture PRIVATE ${OpenCV_LIBS})
target_link_libraries(capture PRIVATE Threads::Threads)
target_link_libraries(capture PRIVATE spdlog::spdlog)
target_link_libraries(capture PRIVATE libASICamera2.so.1.18)
target_link_libraries(capture PRIVATE libzwo_fixer.so)
