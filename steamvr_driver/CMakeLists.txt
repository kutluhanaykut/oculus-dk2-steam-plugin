# CMake configuration for the DK2 SteamVR driver DLL.

set(DK2VR_DRIVER_SOURCES
    driver_factory.cpp
    device_provider.cpp
    hmd_device_driver.cpp
    driverlog.cpp
    dk2_imu.cpp
)

add_library(driver_dk2vr SHARED ${DK2VR_DRIVER_SOURCES})

target_include_directories(driver_dk2vr PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/openvr/headers
)

target_compile_definitions(driver_dk2vr PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0601
    OPENVR_BUILD
)

target_link_libraries(driver_dk2vr PRIVATE
    libusb_static
    hidapi::hidapi
    setupapi
    winusb
    advapi32
)

if(MSVC)
    target_compile_options(driver_dk2vr PRIVATE /W4 /permissive- /utf-8 /EHsc)
else()
    target_compile_options(driver_dk2vr PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The manifest declares dllPath "bin/win64/driver_dk2vr.dll", so the DLL must
# be emitted under bin/win64/ relative to the driver root. Multi-config
# generators (Visual Studio) append $(Configuration) to the plain output
# directory, so set per-config overrides to keep the DLL at the exact path
# SteamVR expects.
set_target_properties(driver_dk2vr PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/drivers/dk2vr/bin/win64"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/drivers/dk2vr/bin/win64"
)
foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    string(TOLOWER "${_cfg}" _cfg_lower)
    set_target_properties(driver_dk2vr PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/bin/drivers/dk2vr/bin/win64"
        LIBRARY_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/bin/drivers/dk2vr/bin/win64"
    )
endforeach()

# Copy the driver manifest to the driver root (SteamVR expects the manifest at
# drivers/<name>/driver.vrdrivermanifest, one level above the bin/ folder that
# holds the DLL).
add_custom_command(TARGET driver_dk2vr POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/driver.vrdrivermanifest"
        "${CMAKE_BINARY_DIR}/bin/drivers/dk2vr/driver.vrdrivermanifest"
    VERBATIM
)
