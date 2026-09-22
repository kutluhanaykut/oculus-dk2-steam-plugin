cmake_minimum_required(VERSION 3.24)

project(DK2VR VERSION 0.2.0 LANGUAGES C CXX RC)

# CMake 4 removed implicit compatibility below 3.5. Some pinned, otherwise
# unchanged upstream dependencies still declare an older policy baseline.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum policy for legacy dependencies" FORCE)

if(NOT WIN32)
    message(FATAL_ERROR "The DK2 SteamVR driver currently targets Windows 10/11 only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build dependencies statically" FORCE)
if(MSVC)
    # The driver DLL is loaded by vrserver.exe, which does not ship our CRT.
    # Static linking keeps the DLL self-contained.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(DK2VR_BUILD_TESTS "Build unit tests" ON)
option(DK2VR_BUILD_INSTALLER "Build the SteamVR driver installer EXE" ON)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---------------------------------------------------------------------------
# OpenVR. Only the headers are needed: a driver DLL is loaded by vrserver and
# never links against openvr_api.lib, so the SDK itself is not built.
#
# A copy already sitting in third_party/openvr is used as-is. Otherwise the
# headers are fetched. The upstream repository carries a lot of prebuilt
# binaries, so SOURCE_SUBDIR points at a directory that does not exist, which
# tells FetchContent to download without adding the project.
# ---------------------------------------------------------------------------
set(DK2VR_LOCAL_OPENVR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openvr/headers")
add_library(openvr_headers INTERFACE)

# SYSTEM, because openvr_driver.h's empty default implementations trip /W4's
# unreferenced-parameter warning dozens of times and bury our own output.
if(EXISTS "${DK2VR_LOCAL_OPENVR}/openvr_driver.h")
    message(STATUS "Using the OpenVR headers already in third_party/openvr")
    target_include_directories(openvr_headers SYSTEM INTERFACE "${DK2VR_LOCAL_OPENVR}")
else()
    FetchContent_Declare(
        openvr
        GIT_REPOSITORY https://github.com/ValveSoftware/openvr.git
        GIT_TAG v2.5.1
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR cmake-do-not-add-upstream-project
    )
    FetchContent_MakeAvailable(openvr)
    target_include_directories(openvr_headers SYSTEM INTERFACE ${openvr_SOURCE_DIR}/headers)
endif()

# ---------------------------------------------------------------------------
# libusb-1.0 talks to the DK2 tracker through the WinUSB or libusb-win32
# driver. v1.0.27 has no CMake build system, so the Windows backend sources
# are compiled here directly.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    libusb
    GIT_REPOSITORY https://github.com/libusb/libusb.git
    GIT_TAG v1.0.27
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR cmake-do-not-add-upstream-project
)
FetchContent_MakeAvailable(libusb)

add_library(libusb_static STATIC
    ${libusb_SOURCE_DIR}/libusb/core.c
    ${libusb_SOURCE_DIR}/libusb/descriptor.c
    ${libusb_SOURCE_DIR}/libusb/hotplug.c
    ${libusb_SOURCE_DIR}/libusb/io.c
    ${libusb_SOURCE_DIR}/libusb/strerror.c
    ${libusb_SOURCE_DIR}/libusb/sync.c
    ${libusb_SOURCE_DIR}/libusb/os/windows_winusb.c
    ${libusb_SOURCE_DIR}/libusb/os/windows_common.c
    ${libusb_SOURCE_DIR}/libusb/os/windows_usbdk.c
    ${libusb_SOURCE_DIR}/libusb/os/events_windows.c
    ${libusb_SOURCE_DIR}/libusb/os/threads_windows.c
)
target_include_directories(libusb_static PUBLIC
    ${libusb_SOURCE_DIR}
    ${libusb_SOURCE_DIR}/libusb
    ${libusb_SOURCE_DIR}/libusb/os
    ${libusb_SOURCE_DIR}/msvc
)
target_compile_definitions(libusb_static PRIVATE
    _WIN32_WINNT=0x0601
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    PLATFORM_WINDOWS=1
    _TIMESPEC_DEFINED=1
)
target_link_libraries(libusb_static PUBLIC setupapi advapi32 winusb)

# ---------------------------------------------------------------------------
# hidapi is the fallback transport for DK2 units bound to the Windows HID
# driver rather than WinUSB.
# ---------------------------------------------------------------------------
set(HIDAPI_BUILD_HIDTEST OFF CACHE BOOL "" FORCE)
set(HIDAPI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    hidapi
    GIT_REPOSITORY https://github.com/libusb/hidapi.git
    GIT_TAG hidapi-0.14.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(hidapi)

# hidapi exports an ALIAS target, and CMake will not alias an alias, so the
# name is carried in a variable instead.
if(TARGET hidapi::hidapi)
    set(DK2VR_HIDAPI_TARGET hidapi::hidapi)
elseif(TARGET hidapi_winapi)
    set(DK2VR_HIDAPI_TARGET hidapi_winapi)
elseif(TARGET hidapi)
    set(DK2VR_HIDAPI_TARGET hidapi)
else()
    message(FATAL_ERROR "A suitable hidapi target was not created")
endif()
message(STATUS "Using hidapi target: ${DK2VR_HIDAPI_TARGET}")

# ---------------------------------------------------------------------------
# dk2vr_core: the hardware- and OpenVR-independent logic. Kept in its own
# library so the unit tests can exercise it without SteamVR or a headset.
# ---------------------------------------------------------------------------
add_library(dk2vr_core STATIC
    core/display_geometry.cpp
    core/imu_packet.cpp
    core/orientation_filter.cpp
    core/lens_distortion.cpp
)
target_include_directories(dk2vr_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(dk2vr_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
target_link_libraries(dk2vr_core PUBLIC user32)
if(MSVC)
    target_compile_options(dk2vr_core PRIVATE /W4 /permissive- /utf-8 /EHsc)
else()
    target_compile_options(dk2vr_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_subdirectory(steamvr_driver)
add_subdirectory(tools)

if(DK2VR_BUILD_INSTALLER)
    add_subdirectory(installer)
endif()

if(DK2VR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
