# Standalone display diagnostic. Runs without SteamVR and without elevation.
add_executable(DK2DisplayReport display_report.cpp)

target_link_libraries(DK2DisplayReport PRIVATE dk2vr_core)

if(MSVC)
    target_compile_options(DK2DisplayReport PRIVATE /W4 /permissive- /utf-8 /EHsc)
else()
    target_compile_options(DK2DisplayReport PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(DK2DisplayReport PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
