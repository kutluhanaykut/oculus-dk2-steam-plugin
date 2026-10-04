#include "core/display_geometry.h"

#include <Windows.h>

#include <algorithm>
#include <sstream>

namespace dk2vr {
namespace {

// The DK2's monitor reports this hardware id in its EDID.
constexpr wchar_t kDk2MonitorId[] = L"OVR0003";

std::string narrow(const std::wstring& text)
{
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

// DEVMODE stores the orientation as a quarter-turn count; degrees are easier
// to reason about everywhere else.
std::int32_t orientationToDegrees(const DWORD orientation)
{
    switch (orientation) {
    case DMDO_90:
        return 90;
    case DMDO_180:
        return 180;
    case DMDO_270:
        return 270;
    case DMDO_DEFAULT:
    default:
        return 0;
    }
}

} // namespace

ImageRotation rotationFromDegrees(const int degrees)
{
    // Accept negative and over-wound values so "-90" and "270" both work.
    int normalized = degrees % 360;
    if (normalized < 0) {
        normalized += 360;
    }
    switch (normalized) {
    case 90:
        return ImageRotation::Cw90;
    case 180:
        return ImageRotation::Cw180;
    case 270:
        return ImageRotation::Cw270;
    default:
        return ImageRotation::None;
    }
}

DisplayLayout computeLayout(const LayoutRequest& request)
{
    DisplayLayout layout;
    layout.windowX = request.x;
    layout.windowY = request.y;
    layout.windowWidth = (std::max)(request.width, 2);
    layout.windowHeight = (std::max)(request.height, 2);
    layout.swapEyes = request.swapEyes;

    const bool landscape = layout.windowWidth >= layout.windowHeight;

    if (request.splitMode == kSplitAuto) {
        // The lenses run along the framebuffer's longer axis.
        layout.split = landscape ? EyeSplit::Horizontal : EyeSplit::Vertical;
    } else {
        layout.split = request.splitMode == 0 ? EyeSplit::Horizontal : EyeSplit::Vertical;
    }

    if (request.rotationDegrees == kRotationAuto) {
        // Only the shape of the framebuffer matters here. Windows' own
        // rotation is deliberately not compensated for: measuring on a DK2
        // whose panel Windows turns by 90 degrees showed the image upright
        // with no rotation at all, which means SteamVR presents through the
        // desktop's coordinate space and inherits that rotation rather than
        // bypassing it. Repeating the turn put the view back on its side.
        //
        // A portrait framebuffer still needs turning, because then the panel
        // is being scanned out along the axis the lenses do not run on.
        layout.rotation = landscape ? ImageRotation::None : ImageRotation::Cw90;
    } else {
        layout.rotation = rotationFromDegrees(request.rotationDegrees);
    }

    // Per-eye viewport, before any rotation.
    const std::int32_t viewportWidth = layout.split == EyeSplit::Horizontal
        ? layout.windowWidth / 2
        : layout.windowWidth;
    const std::int32_t viewportHeight = layout.split == EyeSplit::Horizontal
        ? layout.windowHeight
        : layout.windowHeight / 2;

    // A quarter turn swaps the render target's axes: the eye is rendered
    // upright and only turned when the compositor samples it onto the panel.
    const bool quarterTurn = layout.rotation == ImageRotation::Cw90
        || layout.rotation == ImageRotation::Cw270;
    layout.renderWidth = quarterTurn ? viewportHeight : viewportWidth;
    layout.renderHeight = quarterTurn ? viewportWidth : viewportHeight;

    return layout;
}

Uv unrotate(const ImageRotation rotation, const Uv output)
{
    switch (rotation) {
    case ImageRotation::Cw90:
        // Image turned 90 clockwise: output top-right shows render top-left.
        return Uv {output.v, 1.0F - output.u};
    case ImageRotation::Cw180:
        return Uv {1.0F - output.u, 1.0F - output.v};
    case ImageRotation::Cw270:
        return Uv {1.0F - output.v, output.u};
    case ImageRotation::None:
    default:
        return output;
    }
}

EyeViewport eyeViewport(const DisplayLayout& layout, const bool isLeftEye)
{
    const bool first = layout.swapEyes ? !isLeftEye : isLeftEye;

    EyeViewport viewport;
    if (layout.split == EyeSplit::Horizontal) {
        const auto half = static_cast<std::uint32_t>(layout.windowWidth / 2);
        viewport.x = first ? 0U : half;
        viewport.y = 0U;
        viewport.width = half;
        viewport.height = static_cast<std::uint32_t>(layout.windowHeight);
    } else {
        const auto half = static_cast<std::uint32_t>(layout.windowHeight / 2);
        viewport.x = 0U;
        viewport.y = first ? 0U : half;
        viewport.width = static_cast<std::uint32_t>(layout.windowWidth);
        viewport.height = half;
    }
    return viewport;
}

DisplayInfo findDk2Display(std::string* diagnostics)
{
    std::ostringstream report;
    DisplayInfo result;

    DISPLAY_DEVICEW adapter {};
    adapter.cb = sizeof(adapter);

    for (DWORD index = 0; EnumDisplayDevicesW(nullptr, index, &adapter, 0); ++index) {
        adapter.cb = sizeof(adapter);

        const bool attached = (adapter.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0;

        DEVMODEW mode {};
        mode.dmSize = sizeof(mode);
        const bool haveMode = EnumDisplaySettingsW(adapter.DeviceName,
            ENUM_CURRENT_SETTINGS, &mode) != FALSE;

        // A display device can expose several monitors; check each one for the
        // DK2 hardware id rather than only the first.
        DISPLAY_DEVICEW monitor {};
        monitor.cb = sizeof(monitor);
        bool isDk2 = false;
        std::wstring monitorId;
        std::wstring monitorName;
        for (DWORD monitorIndex = 0;
             EnumDisplayDevicesW(adapter.DeviceName, monitorIndex, &monitor, 0);
             ++monitorIndex) {
            monitor.cb = sizeof(monitor);
            if (monitorId.empty()) {
                monitorId = monitor.DeviceID;
                monitorName = monitor.DeviceString;
            }
            if (wcsstr(monitor.DeviceID, kDk2MonitorId) != nullptr) {
                isDk2 = true;
                monitorId = monitor.DeviceID;
                monitorName = monitor.DeviceString;
                break;
            }
            monitor = {};
            monitor.cb = sizeof(monitor);
        }

        report << "  [" << index << "] " << narrow(adapter.DeviceName)
               << (attached ? " attached" : " DETACHED")
               << " monitor='" << narrow(monitorName) << "'"
               << " id='" << narrow(monitorId) << "'";
        if (haveMode) {
            report << " mode=" << mode.dmPelsWidth << "x" << mode.dmPelsHeight
                   << "@" << mode.dmDisplayFrequency << "Hz"
                   << " pos=(" << mode.dmPosition.x << "," << mode.dmPosition.y << ")"
                   << " rotation=" << orientationToDegrees(mode.dmDisplayOrientation);
        } else {
            report << " mode=<unavailable>";
        }
        report << (isDk2 ? "   <-- DK2\n" : "\n");

        if (isDk2 && attached && haveMode && !result.found) {
            result.found = true;
            result.deviceName = adapter.DeviceName;
            result.monitorId = narrow(monitorId);
            result.monitorName = narrow(monitorName);
            result.x = mode.dmPosition.x;
            result.y = mode.dmPosition.y;
            result.width = static_cast<std::int32_t>(mode.dmPelsWidth);
            result.height = static_cast<std::int32_t>(mode.dmPelsHeight);
            result.refreshHz = static_cast<std::int32_t>(mode.dmDisplayFrequency);
            result.windowsRotationDegrees = orientationToDegrees(mode.dmDisplayOrientation);
        }

        adapter = {};
        adapter.cb = sizeof(adapter);
    }

    if (diagnostics != nullptr) {
        *diagnostics = report.str();
    }
    return result;
}

std::vector<DisplayMode> enumerateDisplayModes(const std::wstring& deviceName)
{
    std::vector<DisplayMode> modes;
    DEVMODEW mode {};
    mode.dmSize = sizeof(mode);
    for (DWORD index = 0; EnumDisplaySettingsW(deviceName.c_str(), index, &mode); ++index) {
        DisplayMode entry;
        entry.width = static_cast<std::int32_t>(mode.dmPelsWidth);
        entry.height = static_cast<std::int32_t>(mode.dmPelsHeight);
        entry.refreshHz = static_cast<std::int32_t>(mode.dmDisplayFrequency);
        entry.bitsPerPixel = static_cast<std::int32_t>(mode.dmBitsPerPel);

        // The list repeats every mode once per colour depth and per rotation;
        // only distinct geometry matters here.
        const bool seen = std::any_of(modes.begin(), modes.end(),
            [&entry](const DisplayMode& other) {
                return other.width == entry.width && other.height == entry.height
                    && other.refreshHz == entry.refreshHz;
            });
        if (!seen) {
            modes.push_back(entry);
        }
        mode = {};
        mode.dmSize = sizeof(mode);
    }
    return modes;
}

const char* toString(const ImageRotation rotation)
{
    switch (rotation) {
    case ImageRotation::Cw90:
        return "90 deg CW";
    case ImageRotation::Cw180:
        return "180 deg";
    case ImageRotation::Cw270:
        return "270 deg CW";
    case ImageRotation::None:
    default:
        return "none";
    }
}

const char* toString(const EyeSplit split)
{
    return split == EyeSplit::Horizontal ? "side-by-side" : "over-under";
}

} // namespace dk2vr
