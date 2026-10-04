// Display geometry for the DK2 panel.
//
// The DK2 is used in extended-desktop mode, so SteamVR renders into a region
// of the Windows desktop. Two things have to be right for the image to look
// correct through the lenses:
//
//   1. The region must match the DK2's actual position and mode. Hard-coding
//      1920x1080 at (1920, 0) breaks as soon as the user has a different
//      monitor arrangement, a scaled desktop, or the DK2 set to a non-native
//      mode.
//   2. The two eye images must be laid out along the panel's *physical* lens
//      axis, and rotated to match how the panel is scanned out. Depending on
//      the EDID mode the DK2 negotiated and on the Windows rotation setting,
//      the logical framebuffer may be portrait while the lenses sit side by
//      side, or vice versa.
//
// Everything in this header is pure computation so it can be unit tested
// without a headset; the Windows enumeration lives in the .cpp behind
// findDk2Display().
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dk2vr {

// How the rendered image is rotated on its way to the panel. The value is the
// clockwise rotation applied to the image as the viewer sees it.
enum class ImageRotation {
    None = 0,
    Cw90 = 90,
    Cw180 = 180,
    Cw270 = 270,
};

// How the panel area is divided between the two eyes.
enum class EyeSplit {
    Horizontal, // left eye on the left half
    Vertical,   // left eye on the top half
};

// Sentinel meaning "work it out from the detected mode".
inline constexpr int kRotationAuto = -1;
inline constexpr int kSplitAuto = -1;

// A UV coordinate in [0, 1].
struct Uv {
    float u {0.0F};
    float v {0.0F};
};

// What Windows reports about the DK2 panel.
struct DisplayInfo {
    bool found {false};
    std::wstring deviceName;  // e.g. "\\\\.\\DISPLAY2"
    std::string monitorId;    // e.g. "MONITOR\\OVR0003\\..."
    std::string monitorName;  // adapter-supplied description
    std::int32_t x {0};
    std::int32_t y {0};
    std::int32_t width {0};
    std::int32_t height {0};
    std::int32_t refreshHz {0};
    // Windows' rotation for this display, in degrees clockwise (0/90/180/270).
    std::int32_t windowsRotationDegrees {0};
};

// The resolved layout handed to SteamVR.
struct DisplayLayout {
    std::int32_t windowX {0};
    std::int32_t windowY {0};
    std::int32_t windowWidth {0};
    std::int32_t windowHeight {0};
    // Per-eye render target. With a 90/270 degree rotation this is the
    // viewport with its axes swapped, because the eye is rendered upright and
    // only turned when it is sampled onto the panel.
    std::int32_t renderWidth {0};
    std::int32_t renderHeight {0};
    EyeSplit split {EyeSplit::Horizontal};
    ImageRotation rotation {ImageRotation::None};
    bool swapEyes {false};
};

// Inputs to computeLayout. rotationDegrees and splitMode accept the kAuto
// sentinels; anything else is taken literally so a user can override a wrong
// guess from steamvr.vrsettings.
struct LayoutRequest {
    std::int32_t x {0};
    std::int32_t y {0};
    std::int32_t width {0};
    std::int32_t height {0};
    // Windows' own rotation for this display, in degrees clockwise (0, 90,
    // 180, 270). The DK2's panel scans out portrait, so Windows is usually
    // rotating it to present a landscape desktop.
    int windowsRotationDegrees {0};
    int rotationDegrees {kRotationAuto};
    int splitMode {kSplitAuto};
    bool swapEyes {false};
};

// Resolves a layout. Pure; safe to unit test.
//
// Two things drive the automatic choice:
//
//   * The lenses run along the panel's longer *physical* axis. For the DK2
//     that means the eyes sit side by side across the 1920 pixel direction.
//   * SteamVR presents to the display through DXGI rather than through the
//     desktop, which bypasses whatever rotation Windows applies. So when
//     Windows is rotating the panel, the driver has to rotate the image by
//     the same amount to put it back where the desktop would have had it.
//
// Both can be overridden, because whether a given SteamVR build honours the
// desktop rotation is not something the driver can detect.
DisplayLayout computeLayout(const LayoutRequest& request);

// Converts a rotation in degrees to the enum, snapping to the nearest
// quarter turn. Returns ImageRotation::None for values that make no sense.
ImageRotation rotationFromDegrees(int degrees);

// Maps a coordinate in the eye's output viewport to the coordinate in the
// eye's render target that should be sampled, undoing `rotation`.
//
// This is the inverse of the rotation: if the image is shown rotated 90
// degrees clockwise, the pixel in the top-right of the output comes from the
// top-left of the render target.
Uv unrotate(ImageRotation rotation, Uv output);

// Per-eye output viewport within the panel, in pixels.
struct EyeViewport {
    std::uint32_t x {0};
    std::uint32_t y {0};
    std::uint32_t width {0};
    std::uint32_t height {0};
};

// isLeftEye selects the eye; the layout's swapEyes flag is honoured here.
EyeViewport eyeViewport(const DisplayLayout& layout, bool isLeftEye);

// Enumerates the Windows displays and returns the one whose monitor reports
// the Oculus DK2 hardware id (OVR0003). `diagnostics`, when non-null, is
// filled with a human readable dump of every display considered, which is
// what the driver writes to its log when detection fails.
DisplayInfo findDk2Display(std::string* diagnostics);

// One mode the panel advertises.
struct DisplayMode {
    std::int32_t width {0};
    std::int32_t height {0};
    std::int32_t refreshHz {0};
    std::int32_t bitsPerPixel {0};
};

// Every mode Windows lists for a display device, newest-first order as
// reported. SteamVR has to put the compositor into fullscreen on the panel,
// and that only works for a mode the output actually advertises, so knowing
// the list is the difference between guessing and knowing.
std::vector<DisplayMode> enumerateDisplayModes(const std::wstring& deviceName);

// Human readable names, used in log output.
const char* toString(ImageRotation rotation);
const char* toString(EyeSplit split);

} // namespace dk2vr
