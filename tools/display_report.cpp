// Prints what the driver sees when it looks for the DK2 panel.
//
// The driver logs the same information into vrserver.txt at startup, but that
// means starting SteamVR to find out why SteamVR is not working. This runs on
// its own, needs no elevation, and touches nothing.
#include <Windows.h>

#include <cstdio>
#include <string>
#include <vector>

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

    const std::vector<dk2vr::DisplayMode> modes =
        dk2vr::enumerateDisplayModes(info.deviceName);
    std::printf("Modes this panel advertises (%d distinct):\n",
        static_cast<int>(modes.size()));
    bool currentIsAdvertised = false;
    for (const dk2vr::DisplayMode& mode : modes) {
        const bool current = mode.width == info.width && mode.height == info.height
            && mode.refreshHz == info.refreshHz;
        currentIsAdvertised = currentIsAdvertised || current;
        std::printf("  %5dx%-5d %3d Hz%s\n", mode.width, mode.height, mode.refreshHz,
            current ? "   <-- in use" : "");
    }

    std::printf("\nSteamVR has to put its compositor into fullscreen on this panel.\n");
    std::printf("That only works for a mode the output really advertises; when it\n");
    std::printf("fails, the headset is left filled with solid dark red and SteamVR\n");
    std::printf("offers an \"Enable fullscreen mode\" link in its status window.\n");
    if (!currentIsAdvertised) {
        std::printf("\n  WARNING: the mode in use is not in the advertised list above.\n");
    }
    if (info.windowsRotationDegrees != 0) {
        std::printf("\n  NOTE: Windows is rotating this panel by %d degrees, so its raw\n",
            info.windowsRotationDegrees);
        std::printf("  scanout is %dx%d. A rotated output is a common reason for the\n",
            info.height, info.width);
        std::printf("  fullscreen transition to fail. Setting the DK2's orientation in\n");
        std::printf("  Windows so this reads 0 degrees is worth trying; the driver\n");
        std::printf("  handles the resulting portrait framebuffer on its own.\n");
    }
    std::printf("\n");

    dk2vr::LayoutRequest request;
    request.x = info.x;
    request.y = info.y;
    request.width = info.width;
    request.height = info.height;
    request.windowsRotationDegrees = info.windowsRotationDegrees;

    std::printf("Layout the driver would use:\n");
    printLayout("auto", request);

    std::printf("\nIf the view is turned on its side, set one of these in the\n");
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
