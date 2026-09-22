add_executable(DK2VRTests
    test_main.cpp
    test_imu_packet.cpp
    test_orientation.cpp
    test_display_geometry.cpp
    test_distortion.cpp
)

target_link_libraries(DK2VRTests PRIVATE dk2vr_core)

if(MSVC)
    target_compile_options(DK2VRTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
else()
    target_compile_options(DK2VRTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(DK2VRTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(NAME DK2VRTests COMMAND DK2VRTests)
