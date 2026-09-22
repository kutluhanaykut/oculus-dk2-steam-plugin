# The DK2 SteamVR driver DLL.

add_library(driver_dk2vr SHARED
    driver_factory.cpp
    device_provider.cpp
    driver_settings.cpp
    hmd_device_driver.cpp
    controller_device_driver.cpp
    input_source.cpp
    mouse_wheel.cpp
    dk2_imu.cpp
    driverlog.cpp
)

target_include_directories(driver_dk2vr PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(driver_dk2vr PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0601
)

target_link_libraries(driver_dk2vr PRIVATE
    dk2vr_core
    openvr_headers
    libusb_static
    ${DK2VR_HIDAPI_TARGET}
    setupapi
    winusb
    advapi32
    user32
)

if(MSVC)
    target_compile_options(driver_dk2vr PRIVATE /W4 /permissive- /utf-8 /EHsc)
else()
    target_compile_options(driver_dk2vr PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The manifest declares dllPath "bin/win64/driver_dk2vr.dll", so the DLL has to
# land under bin/win64/ relative to the driver root. Multi-config generators
# append $(Configuration) to the plain output directory, hence the per-config
# overrides.
set(DK2VR_DRIVER_ROOT "${CMAKE_BINARY_DIR}/bin/drivers/dk2vr")
set_target_properties(driver_dk2vr PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${DK2VR_DRIVER_ROOT}/bin/win64"
    LIBRARY_OUTPUT_DIRECTORY "${DK2VR_DRIVER_ROOT}/bin/win64"
)
foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(driver_dk2vr PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${DK2VR_DRIVER_ROOT}/bin/win64"
        LIBRARY_OUTPUT_DIRECTORY_${_cfg} "${DK2VR_DRIVER_ROOT}/bin/win64"
    )
endforeach()

# SteamVR expects the manifest at drivers/<name>/driver.vrdrivermanifest, one
# level above the bin/ folder that holds the DLL.
add_custom_command(TARGET driver_dk2vr POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/driver.vrdrivermanifest"
        "${DK2VR_DRIVER_ROOT}/driver.vrdrivermanifest"
    VERBATIM
)
