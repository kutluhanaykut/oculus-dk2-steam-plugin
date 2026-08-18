# CMake configuration for the DK2 SteamVR driver installer EXE.
# The installer locates SteamVR, copies the driver files into SteamVR's
# drivers directory, and runs a set of post-install sanity checks.

set(DK2VR_INSTALLER_SOURCES
    main.cpp
    installer.rc
)

add_executable(DK2SteamVRDriverInstaller ${DK2VR_INSTALLER_SOURCES})

target_include_directories(DK2SteamVRDriverInstaller PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/openvr/headers
)

target_compile_definitions(DK2SteamVRDriverInstaller PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0601
)

target_link_libraries(DK2SteamVRDriverInstaller PRIVATE
    libusb_static
    hidapi::hidapi
    setupapi
    winusb
    advapi32
    shell32
)

if(MSVC)
    target_compile_options(DK2SteamVRDriverInstaller PRIVATE /W4 /permissive- /utf-8 /EHsc)
else()
    target_compile_options(DK2SteamVRDriverInstaller PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(DK2SteamVRDriverInstaller PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:DK2SteamVRDriverInstaller>"
)
