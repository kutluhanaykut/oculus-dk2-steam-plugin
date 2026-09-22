// Button and axis state for the virtual controller.
//
// The DK2 has no controllers of its own, and SteamVR has not accepted a
// gamepad as a pointing device since the legacy input system was retired. So
// that the dashboard and games can be operated at all, the driver reads an
// XInput pad directly and feeds it into a controller device whose pose
// follows the user's gaze.
//
// The keyboard is a fallback for when no pad is plugged in. Its bindings all
// require Ctrl+Alt to be held, because a driver inside vrserver.exe sees key
// state regardless of which window has focus and must not react to ordinary
// typing.
#pragma once

#include <cstdint>

#include "mouse_wheel.h"

namespace dk2vr {

struct InputState {
    bool triggerClick {false};
    float triggerValue {0.0F};
    bool gripClick {false};
    bool menuClick {false};
    bool systemClick {false};
    bool padClick {false};
    bool padTouch {false};
    float padX {0.0F};
    float padY {0.0F};

    // Aiming the hand independently of the head, so it can be reached out to
    // grab things rather than staying pinned to the middle of the view.
    // Both are rates in -1..1, not absolute angles.
    float aimX {0.0F};
    float aimY {0.0F};
    // Positive pushes the hand away, negative pulls it in. A rate, scaled by
    // elapsed time.
    float distanceDelta {0.0F};
    // A one-shot nudge in metres, from the mouse wheel. Applied directly
    // rather than over time, because a wheel notch is an amount, not a rate.
    float distanceImpulseMeters {0.0F};
    // Returns the hand to the centre of the view.
    bool resetAim {false};

    // Turns the whole world around the user, in -1..1. This is how "forward"
    // gets lined up with a game that decided its own, since a camera-less
    // DK2 has no absolute yaw reference to agree on.
    float yawAdjust {0.0F};

    // Rising edge of the recentre chord, so the caller does not have to
    // debounce it.
    bool recenterPressed {false};

    bool gamepadConnected {false};
    bool usingKeyboard {false};
};

class InputSource {
public:
    void configure(bool gamepadEnabled, bool keyboardEnabled, bool mouseEnabled,
        float wheelStepMeters, bool trackpadRequiresModifier, bool directModeAtStart);

    // True while every binding is live without Ctrl+Alt.
    bool directMode() const noexcept { return directMode_; }

    // Reads the current state. Cheap enough to call once per frame; XInput is
    // only queried for the first connected pad, and unplugged slots are
    // re-probed at a slow rate rather than every call.
    InputState poll();

private:
    bool pollGamepad(InputState& state);
    bool pollKeyboardAndMouse(InputState& state);
    // Writes a line to the driver log whenever a button changes, so a report
    // of "the button does nothing" can be split into "the driver never saw
    // it" and "SteamVR did not route it".
    void logChanges(const InputState& state);

    bool gamepadEnabled_ {true};
    bool keyboardEnabled_ {true};
    bool mouseEnabled_ {true};
    int lastGamepadIndex_ {-1};
    std::uint64_t nextProbeMs_ {0};
    // Separate edge state per source: both are polled every frame now, so a
    // shared flag would let one source swallow the other's press.
    bool recenterHeldPad_ {false};
    bool recenterHeldKey_ {false};
    InputState previous_ {};

    // Mouse aiming works from cursor movement while the modifier is held, so
    // the previous position has to be remembered between polls.
    bool haveCursor_ {false};
    long lastCursorX_ {0};
    long lastCursorY_ {0};

    MouseWheelReader wheel_;
    float wheelStepMeters_ {0.05F};

    // Movement keys are the ones you hold while the other hand is on the
    // mouse, so by default they do not also demand a modifier.
    bool trackpadRequiresModifier_ {false};
    // Toggled with Ctrl+Alt+V: drops the modifier requirement from every
    // binding, for when both hands are already busy.
    bool directMode_ {false};
    bool toggleHeld_ {false};
};

} // namespace dk2vr
