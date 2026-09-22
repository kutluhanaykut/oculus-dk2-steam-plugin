// Every tunable the driver reads from steamvr.vrsettings, in one place.
//
// The manifest ships defaults, but SteamVR does not always copy them into
// steamvr.vrsettings, so each read falls back to a value that works on a
// stock DK2. Users edit these in
//   Steam/config/steamvr.vrsettings
// under the "driver_dk2vr", "dk2vr_display" and "dk2vr_input" sections.
#pragma once

#include <string>

#include "core/display_geometry.h"
#include "core/lens_distortion.h"

namespace dk2vr {

struct DriverSettings {
    // --- identity ---
    std::string modelNumber {"Oculus Rift DK2"};
    std::string serialNumber {"DK2VR0001"};

    // --- display ---
    // Used only when the DK2 monitor cannot be found on the desktop.
    std::int32_t fallbackWindowX {0};
    std::int32_t fallbackWindowY {0};
    std::int32_t fallbackWindowWidth {1920};
    std::int32_t fallbackWindowHeight {1080};

    // kRotationAuto, or 0 / 90 / 180 / 270. This is the knob to turn when the
    // image looks sideways in the headset.
    int imageRotationDegrees {kRotationAuto};
    // kSplitAuto, 0 for side-by-side eyes, 1 for over-under.
    int eyeSplitMode {kSplitAuto};
    bool swapEyes {false};

    // Multiplies the per-eye render target. Above 1 costs GPU time but
    // recovers detail lost to the distortion warp.
    float supersampleScale {1.0F};

    // Overrides the refresh rate reported to the compositor. 0 means "use
    // whatever Windows says the DK2 is running at".
    float displayFrequencyOverride {0.0F};

    // --- optics ---
    float screenWidthMeters {0.12576F};
    float screenHeightMeters {0.07074F};
    float lensSeparationMeters {0.0635F};
    float eyeReliefMeters {0.041F};
    DistortionCoefficients distortion {};
    bool distortionEnabled {true};
    // Multiplies the field of view. The geometric tangents alone give a
    // narrower view than the DK2 really has, because the lenses magnify; this
    // is that magnification, kept separate from the distortion so the two can
    // be tuned against each other. Lower it when the view bulges.
    float fovScale {1.45F};

    // --- tracking ---
    // Pitch/roll correction strength, in inverse seconds. 0 leaves pure gyro
    // integration, which drifts.
    double gravityGain {1.0};
    // Height the stationary HMD is reported at, in metres.
    float seatedHeightMeters {1.6F};
    // Manual correction for which way is forward, in degrees. Written back by
    // the driver whenever it is adjusted, so the alignment is done once.
    float yawOffsetDegrees {0.0F};
    float yawAdjustSpeedDegreesPerSecond {45.0F};

    // --- input ---
    bool controllerEnabled {true};
    bool gamepadEnabled {true};
    bool keyboardEnabled {true};
    // Ctrl+Alt plus a mouse button. The most reliable fallback when no
    // gamepad is present.
    bool mouseEnabled {true};
    // How far in front of the eyes the gaze pointer sits, in metres.
    float controllerDistanceMeters {0.35F};
    // Downward tilt applied to the pointer so it does not sit dead centre.
    float controllerPitchOffsetDegrees {0.0F};
    // How fast the right stick or the mouse swings the hand away from the
    // gaze direction, and how far it is allowed to get.
    float aimSpeedDegreesPerSecond {90.0F};
    float aimLimitDegrees {60.0F};
    // How fast the shoulder buttons reach the hand out, and the range it can
    // cover. The far end is roughly an arm's length.
    float reachSpeedMetersPerSecond {0.6F};
    float minDistanceMeters {0.15F};
    float maxDistanceMeters {0.9F};
    // How far one mouse wheel notch moves the hand.
    float wheelStepMeters {0.05F};
    // Whether the movement keys also need Ctrl+Alt. Off by default: holding a
    // chord, a movement key and the mouse at once takes three hands.
    bool trackpadRequiresModifier {false};
    // Start with every binding modifier-free. Ctrl+Alt+V toggles it at any
    // time; this only picks the initial state.
    bool directInputMode {false};
};

// Reads the settings through vr::VRSettings(). Safe to call once the driver
// context is initialised.
DriverSettings loadDriverSettings();

// Multi-line dump for the driver log.
std::string describeSettings(const DriverSettings& settings);

} // namespace dk2vr
