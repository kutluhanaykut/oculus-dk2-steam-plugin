// Prints what the driver sees when it looks for the DK2 panel.
//
// The driver logs the same information into vrserver.txt at startup, but that
// means starting SteamVR to find out why SteamVR is not working. This runs on
// its own, needs no elevation, and touches nothing.
#include <Windows.h>

#include <cstdio>
#include <string>

#include "core/display_geometry.h"

namespace {

void printLayout(const char* label, const dk2vr::LayoutRequest& request)
{
    const dk2vr::DisplayLayout layout = dk2vr::computeLayout(request);
    std::printf("  %-12s window (%d, %d) %dx%d | eyes %s | rotation %s | "
                "per-eye render %dx%d\n",
        label, layout.windowX, layout.windowY, layout.windowWidth, layout.windowHeight,
        dk2vr::toString(layout.split), dk2vr::toString(layout.rotation),
        layout.renderWidth, layout.renderHeight);
}

} // namespace

int main()
{
    SetConsoleOutputCP(CP_UTF8);

    std::printf("DK2 display report\n");
    std::printf("==================\n\n");

    std::string diagnostics;
    const dk2vr::DisplayInfo info = dk2vr::findDk2Display(&diagnostics);

    std::printf("Displays Windows reports:\n%s\n", diagnostics.c_str());

    if (!info.found) {
        std::printf("No OVR0003 monitor is attached to the desktop.\n\n");
        std::printf("The DK2 panel is not reaching Windows as an extended display.\n");
        std::printf("Check the HDMI cable, then open Windows display settings and\n");
        std::printf("choose \"Extend these displays\".\n");
        return 1;
    }

    std::printf("DK2 panel found\n");
    std::printf("  device      %ls\n", info.deviceName.c_str());
    std::printf("  monitor     %s\n", info.monitorName.c_str());
    std::printf("  id          %s\n", info.monitorId.c_str());
    std::printf("  mode        %dx%d @ %d Hz\n", info.width, info.height, info.refreshHz);
    std::printf("  position    (%d, %d)\n", info.x, info.y);
    std::printf("  rotation    %d degrees, applied by Windows\n\n",
        info.windowsRotationDegrees);

    if (info.windowsRotationDegrees != 0) {
        std::printf("Windows is rotating this panel, so its raw scanout is %dx%d and\n",
            info.height, info.width);
        std::printf("the desktop you see is turned to fit. SteamVR presents through\n");
        std::printf("DXGI and skips that step, so the driver repeats the turn itself.\n");
        std::printf("That is what the automatic choice below reflects.\n\n");
    }

    dk2vr::LayoutRequest request;
    request.x = info.x;
    request.y = info.y;
    request.width = info.width;
    request.height = info.height;
    request.windowsRotationDegrees = info.windowsRotationDegrees;

    std::printf("Layout the driver would use:\n");
    printLayout("auto", request);

    std::printf("\nIf the view is turned on its side, add one of these to the\n");
    std::printf("dk2vr_display section of Steam\\config\\steamvr.vrsettings:\n\n");
    for (const int degrees : {0, 90, 180, 270}) {
        dk2vr::LayoutRequest option = request;
        option.rotationDegrees = degrees;
        char label[32];
        std::snprintf(label, sizeof(label), "rotation %d", degrees);
        printLayout(label, option);
    }
    std::printf("\n  \"image_rotation\": <0 | 90 | 180 | 270>\n");
    std::printf("\nRestart SteamVR after each change.\n");

    return 0;
}
