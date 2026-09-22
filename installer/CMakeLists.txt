# The DK2 SteamVR driver installer EXE.
#
# It locates SteamVR, copies the driver in, then runs sanity checks and
# prints a display report so a misconfigured DK2 can be spotted without
# digging through the SteamVR logs.

add_executable(DK2SteamVRDriverInstaller
    main.cpp
    installer.rc
)

# The bundled manifest asks for elevation, which the copy into
# Program Files (x86)/Steam/.../SteamVR/drivers requires.
target_sources(DK2SteamVRDriverInstaller PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/installer.manifest)

if(MSVC)
    # Without this the linker emits its own trustInfo fragment saying
    # asInvoker, and mt.exe refuses to merge two different levels.
    target_link_options(DK2SteamVRDriverInstaller PRIVATE /MANIFESTUAC:NO)
endif()

target_include_directories(DK2SteamVRDriverInstaller PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(DK2SteamVRDriverInstaller PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0601
)

target_link_libraries(DK2SteamVRDriverInstaller PRIVATE
    dk2vr_core
    libusb_static
    ${DK2VR_HIDAPI_TARGET}
    setupapi
    winusb
    advapi32
    shell32
    user32
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
