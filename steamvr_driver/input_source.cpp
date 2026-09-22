#include "input_source.h"

#include "driverlog.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace dk2vr {
namespace {

// Declared here rather than pulling in Xinput.h, so the DLL does not have to
// link an XInput import library. The function is resolved at runtime from
// whichever version of the DLL the machine has.
struct XInputGamepadMini {
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
};

struct XInputStateMini {
    DWORD dwPacketNumber;
    XInputGamepadMini Gamepad;
};

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XInputStateMini*);

constexpr WORD kPadDpadUp = 0x0001;
constexpr WORD kPadDpadDown = 0x0002;
constexpr WORD kPadDpadLeft = 0x0004;
constexpr WORD kPadDpadRight = 0x0008;
constexpr WORD kPadStart = 0x0010;
constexpr WORD kPadBack = 0x0020;
constexpr WORD kPadLeftThumb = 0x0040;
constexpr WORD kPadLeftShoulder = 0x0100;
constexpr WORD kPadRightShoulder = 0x0200;
constexpr WORD kPadA = 0x1000;
constexpr WORD kPadB = 0x2000;
constexpr WORD kPadX = 0x4000;
constexpr WORD kPadY = 0x8000;

constexpr DWORD kErrorDeviceNotConnected = 1167;
constexpr SHORT kThumbDeadzone = 8000;
constexpr BYTE kTriggerThreshold = 60;
constexpr std::uint64_t kProbeIntervalMs = 2000;

// Cursor pixels to aim rate. Tuned so an ordinary mouse sweep covers the
// reachable cone without feeling twitchy.
constexpr double kMouseAimScale = 0.06;

std::uint64_t nowMs()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

XInputGetStateFn loadXInput()
{
    // Newest first; XInput9_1_0 is present on every supported Windows.
    static const wchar_t* candidates[] = {
        L"XInput1_4.dll", L"XInput1_3.dll", L"XInput9_1_0.dll"};
    for (const wchar_t* name : candidates) {
        if (HMODULE module = LoadLibraryW(name)) {
            if (auto* function = reinterpret_cast<XInputGetStateFn>(
                    reinterpret_cast<void*>(GetProcAddress(module, "XInputGetState")))) {
                DriverLog("DK2 input: using %ls for gamepad support.", name);
                return function;
            }
        }
    }
    DriverLog("DK2 input: no XInput DLL found; gamepad support is unavailable.");
    return nullptr;
}

XInputGetStateFn xInputGetState()
{
    static XInputGetStateFn function = loadXInput();
    return function;
}

float normalizeThumb(const SHORT value)
{
    if (std::abs(static_cast<int>(value)) < kThumbDeadzone) {
        return 0.0F;
    }
    const float scaled = static_cast<float>(value) / 32767.0F;
    return std::clamp(scaled, -1.0F, 1.0F);
}

bool keyDown(const int virtualKey)
{
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

// Every keyboard binding sits behind this chord so the driver cannot react to
// ordinary typing in whatever window happens to have focus.
bool modifierHeld()
{
    return keyDown(VK_CONTROL) && keyDown(VK_MENU);
}

} // namespace

void InputSource::configure(const bool gamepadEnabled, const bool keyboardEnabled,
    const bool mouseEnabled, const float wheelStepMeters,
    const bool trackpadRequiresModifier, const bool directModeAtStart)
{
    gamepadEnabled_ = gamepadEnabled;
    keyboardEnabled_ = keyboardEnabled;
    mouseEnabled_ = mouseEnabled;
    wheelStepMeters_ = wheelStepMeters;
    trackpadRequiresModifier_ = trackpadRequiresModifier;
    directMode_ = directModeAtStart;
    if (directMode_) {
        DriverLog("DK2 input: direct mode on at startup; bindings work without Ctrl+Alt.");
    }

    if (mouseEnabled_) {
        wheel_.start();
    } else {
        wheel_.stop();
    }
}

bool InputSource::pollGamepad(InputState& state)
{
    XInputGetStateFn getState = xInputGetState();
    if (getState == nullptr) {
        return false;
    }

    // Stick to the pad that answered last time. Scanning all four slots every
    // frame is measurably slow when some of them are empty, so empty slots
    // are only re-probed every couple of seconds.
    int index = lastGamepadIndex_;
    XInputStateMini padState {};

    if (index >= 0 && getState(static_cast<DWORD>(index), &padState) != ERROR_SUCCESS) {
        index = -1;
        lastGamepadIndex_ = -1;
    }

    if (index < 0) {
        const std::uint64_t now = nowMs();
        if (now < nextProbeMs_) {
            return false;
        }
        nextProbeMs_ = now + kProbeIntervalMs;
        for (DWORD slot = 0; slot < 4; ++slot) {
            const DWORD result = getState(slot, &padState);
            if (result == ERROR_SUCCESS) {
                index = static_cast<int>(slot);
                lastGamepadIndex_ = index;
                DriverLog("DK2 input: gamepad connected in slot %lu.", slot);
                break;
            }
            if (result != kErrorDeviceNotConnected) {
                break;
            }
        }
        if (index < 0) {
            return false;
        }
    }

    const XInputGamepadMini& pad = padState.Gamepad;
    state.gamepadConnected = true;

    const bool triggerPulled = pad.bRightTrigger > kTriggerThreshold;
    state.triggerClick = (pad.wButtons & kPadA) != 0 || triggerPulled;
    state.triggerValue = (std::max)((pad.wButtons & kPadA) != 0 ? 1.0F : 0.0F,
        static_cast<float>(pad.bRightTrigger) / 255.0F);

    state.gripClick = (pad.wButtons & kPadB) != 0;
    state.menuClick = (pad.wButtons & kPadStart) != 0;
    state.systemClick = (pad.wButtons & kPadBack) != 0;
    // Pressing the stick in is the literal trackpad click, but it is an
    // awkward button to find and hold. Y does the same thing and is where a
    // thumb already is, which matters because most teleport gestures are
    // press-and-hold-then-release.
    state.padClick = (pad.wButtons & kPadLeftThumb) != 0
        || (pad.wButtons & kPadY) != 0;

    // Right stick aims the hand away from the centre of the view; the
    // shoulder buttons reach it further out or pull it back in.
    state.aimX = normalizeThumb(pad.sThumbRX);
    state.aimY = normalizeThumb(pad.sThumbRY);
    if ((pad.wButtons & kPadRightShoulder) != 0) {
        state.distanceDelta += 1.0F;
    }
    if ((pad.wButtons & kPadLeftShoulder) != 0) {
        state.distanceDelta -= 1.0F;
    }
    state.resetAim = (pad.wButtons & kPadX) != 0;

    float x = normalizeThumb(pad.sThumbLX);
    float y = normalizeThumb(pad.sThumbLY);
    // The d-pad drives the same axes, which makes menus navigable without
    // fighting a worn analogue stick.
    if ((pad.wButtons & kPadDpadLeft) != 0) {
        x = -1.0F;
    }
    if ((pad.wButtons & kPadDpadRight) != 0) {
        x = 1.0F;
    }
    if ((pad.wButtons & kPadDpadDown) != 0) {
        y = -1.0F;
    }
    if ((pad.wButtons & kPadDpadUp) != 0) {
        y = 1.0F;
    }
    state.padX = x;
    state.padY = y;
    // Resting a finger on the trackpad without pressing or sliding is a
    // distinct signal on a real wand, and plenty of games show their teleport
    // arc on it. Deriving touch purely from the axes made that state
    // unreachable, so the left trigger provides it directly: a natural
    // "brush" against the right trigger's "press".
    const bool touchHeld = pad.bLeftTrigger > kTriggerThreshold;
    state.padTouch = state.padClick || touchHeld || x != 0.0F || y != 0.0F;

    // Back plus the d-pad turns the world, the gamepad equivalent of the
    // Ctrl+Shift+Alt+arrows chord.
    const bool backHeld = (pad.wButtons & kPadBack) != 0;
    if (backHeld && ((pad.wButtons & kPadDpadLeft) != 0
            || (pad.wButtons & kPadDpadRight) != 0)) {
        state.yawAdjust = (pad.wButtons & kPadDpadRight) != 0 ? 1.0F : -1.0F;
        // The d-pad was borrowed for calibration, so it must not also drive
        // the trackpad or open the system menu.
        state.padX = 0.0F;
        state.padY = 0.0F;
        state.padTouch = state.padClick;
        state.systemClick = false;
    }

    // Back and Start together recentres, mirroring what most seated VR
    // wrappers use.
    const bool recenterChord = (pad.wButtons & kPadBack) != 0
        && (pad.wButtons & kPadStart) != 0;
    state.recenterPressed = recenterChord && !recenterHeldPad_;
    recenterHeldPad_ = recenterChord;
    if (recenterChord) {
        // The chord should not also register as menu or system presses.
        state.menuClick = false;
        state.systemClick = false;
    }

    return true;
}

bool InputSource::pollKeyboardAndMouse(InputState& state)
{
    const bool modifier = modifierHeld();

    // Ctrl+Alt+V turns the chord into a mode. Holding a modifier while also
    // working the mouse and a movement key takes three hands, so this lets
    // the modifier be let go of entirely.
    const bool togglePressed = modifier && keyDown('V');
    if (togglePressed && !toggleHeld_) {
        directMode_ = !directMode_;
        DriverLog("DK2 input: direct mode %s.", directMode_ ? "ON" : "OFF");
    }
    toggleHeld_ = togglePressed;

    // Adding Shift to the chord switches the arrows from moving the player to
    // turning the world, which is a setup step rather than a game control.
    const bool calibrating = modifier && keyDown(VK_SHIFT);
    if (calibrating) {
        state.usingKeyboard = true;
        if (keyDown(VK_LEFT)) {
            state.yawAdjust -= 1.0F;
        }
        if (keyDown(VK_RIGHT)) {
            state.yawAdjust += 1.0F;
        }
        // Nothing else fires while calibrating, so the arrows cannot also
        // teleport the player halfway across the room.
        haveCursor_ = false;
        recenterHeldKey_ = false;
        return true;
    }

    const bool active = modifier || directMode_;
    // Movement keys are exempt by default: they are what the spare hand holds
    // while the other one is aiming with the mouse.
    const bool trackpadActive = active || !trackpadRequiresModifier_;

    if (!active && !trackpadActive) {
        recenterHeldKey_ = false;
        haveCursor_ = false;
        return false;
    }

    state.usingKeyboard = true;

    // The trackpad axes are what games use for locomotion, so they get the
    // three key sets people reach for: arrows, WASD and IJKL. The side mouse
    // buttons cover the same ground without leaving the mouse at all.
    float x = 0.0F;
    float y = 0.0F;
    if (trackpadActive) {
        if (keyDown(VK_LEFT) || keyDown('A') || keyDown('J')) {
            x -= 1.0F;
        }
        if (keyDown(VK_RIGHT) || keyDown('D') || keyDown('L')) {
            x += 1.0F;
        }
        if (keyDown(VK_DOWN) || keyDown('S') || keyDown('K')) {
            y -= 1.0F;
        }
        if (keyDown(VK_UP) || keyDown('W') || keyDown('I')) {
            y += 1.0F;
        }
        // Back button pushes the trackpad forward, so the whole teleport
        // gesture is one hand on the mouse.
        if (mouseEnabled_ && keyDown(VK_XBUTTON1)) {
            y += 1.0F;
        }
    }

    // Touch without pressing or sliding: the state a finger resting on the
    // trackpad produces, which several games use on its own to raise a
    // teleport arc. Grouped with the movement keys because it belongs to the
    // same hand.
    bool touchHeld = false;
    if (trackpadActive) {
        touchHeld = keyDown('T');
    }

    x = std::clamp(x, -1.0F, 1.0F);
    y = std::clamp(y, -1.0F, 1.0F);

    if (!active) {
        // Movement only; everything else still wants the modifier.
        haveCursor_ = false;
        recenterHeldKey_ = false;
        state.padX = x;
        state.padY = y;
        state.padClick = mouseEnabled_ && keyDown(VK_XBUTTON2);
        state.padTouch = state.padClick || touchHeld || x != 0.0F || y != 0.0F;
        return true;
    }

    // The mouse is the reliable way to click: a left button press is a single
    // unambiguous key state, where Ctrl+Alt+Enter can collide with window
    // manager and keyboard layout handling.
    const bool mouseTrigger = mouseEnabled_ && keyDown(VK_LBUTTON);
    const bool mouseGrip = mouseEnabled_ && keyDown(VK_RBUTTON);
    // Clicking the wheel puts the hand back where it started, which is the
    // quickest way out of having aimed it somewhere unreachable.
    const bool mouseResetAim = mouseEnabled_ && keyDown(VK_MBUTTON);

    state.triggerClick = mouseTrigger || keyDown(VK_RETURN) || keyDown(VK_SPACE);
    state.triggerValue = state.triggerClick ? 1.0F : 0.0F;
    state.gripClick = mouseGrip || keyDown('G');
    state.menuClick = keyDown('M');
    state.systemClick = keyDown('H');

    state.padX = x;
    state.padY = y;
    // The forward mouse button presses the trackpad, which is what most games
    // treat as "teleport now".
    state.padClick = keyDown('U') || (mouseEnabled_ && keyDown(VK_XBUTTON2));
    state.padTouch = state.padClick || touchHeld || x != 0.0F || y != 0.0F;

    // Moving the mouse while the modifier is held aims the hand. The delta is
    // measured against the last poll, and reset whenever the modifier is
    // released so a stray jump does not fling the hand across the room.
    POINT cursor {};
    if (GetCursorPos(&cursor)) {
        if (haveCursor_) {
            const double dx = static_cast<double>(cursor.x - lastCursorX_);
            const double dy = static_cast<double>(cursor.y - lastCursorY_);
            state.aimX = std::clamp(static_cast<float>(dx * kMouseAimScale), -1.0F, 1.0F);
            state.aimY = std::clamp(static_cast<float>(-dy * kMouseAimScale), -1.0F, 1.0F);
        }
        lastCursorX_ = cursor.x;
        lastCursorY_ = cursor.y;
        haveCursor_ = true;
    }

    if (keyDown(VK_PRIOR)) {
        state.distanceDelta += 1.0F;
    }
    if (keyDown(VK_NEXT)) {
        state.distanceDelta -= 1.0F;
    }
    state.resetAim = mouseResetAim || keyDown('C');

    const bool recenter = keyDown('R');
    state.recenterPressed = recenter && !recenterHeldKey_;
    recenterHeldKey_ = recenter;

    return true;
}

void InputSource::logChanges(const InputState& state)
{
    const auto report = [](const char* name, const bool now, const bool before) {
        if (now != before) {
            DriverLog("DK2 input: %s %s", name, now ? "pressed" : "released");
        }
    };

    report("trigger", state.triggerClick, previous_.triggerClick);
    report("grip", state.gripClick, previous_.gripClick);
    report("menu", state.menuClick, previous_.menuClick);
    report("system", state.systemClick, previous_.systemClick);
    report("trackpad click", state.padClick, previous_.padClick);
    report("trackpad touch", state.padTouch, previous_.padTouch);

    // Axes are quantised before comparing, so a resting stick's jitter does
    // not fill the log, but an actual direction change is visible. Without
    // this it is impossible to tell "the stick was never pushed" apart from
    // "the stick was pushed and the game ignored it".
    const auto quantise = [](const float value) {
        return value > 0.5F ? 1 : (value < -0.5F ? -1 : 0);
    };
    const int padXNow = quantise(state.padX);
    const int padYNow = quantise(state.padY);
    if (padXNow != quantise(previous_.padX) || padYNow != quantise(previous_.padY)) {
        DriverLog("DK2 input: trackpad axis now (%d, %d)", padXNow, padYNow);
    }

    if (state.gamepadConnected != previous_.gamepadConnected) {
        DriverLog("DK2 input: gamepad %s",
            state.gamepadConnected ? "connected" : "disconnected");
    }

    previous_ = state;
}

// Folds a second input source into the first. Buttons are the union, and an
// axis is taken from the extra source only where the primary one is idle, so
// a resting stick never overrides a moving mouse or vice versa.
void mergeInto(InputState& target, const InputState& extra)
{
    target.triggerClick = target.triggerClick || extra.triggerClick;
    target.triggerValue = (std::max)(target.triggerValue, extra.triggerValue);
    target.gripClick = target.gripClick || extra.gripClick;
    target.menuClick = target.menuClick || extra.menuClick;
    target.systemClick = target.systemClick || extra.systemClick;
    target.padClick = target.padClick || extra.padClick;
    target.padTouch = target.padTouch || extra.padTouch;
    target.resetAim = target.resetAim || extra.resetAim;
    target.recenterPressed = target.recenterPressed || extra.recenterPressed;
    target.usingKeyboard = target.usingKeyboard || extra.usingKeyboard;

    if (target.padX == 0.0F) {
        target.padX = extra.padX;
    }
    if (target.padY == 0.0F) {
        target.padY = extra.padY;
    }
    if (target.aimX == 0.0F) {
        target.aimX = extra.aimX;
    }
    if (target.aimY == 0.0F) {
        target.aimY = extra.aimY;
    }
    if (target.distanceDelta == 0.0F) {
        target.distanceDelta = extra.distanceDelta;
    }
    if (target.yawAdjust == 0.0F) {
        target.yawAdjust = extra.yawAdjust;
    }
    target.distanceImpulseMeters += extra.distanceImpulseMeters;
}

InputState InputSource::poll()
{
    InputState state;

    // Both sources are read every frame and combined, so plugging in a
    // gamepad adds to the mouse and keyboard rather than replacing them.
    if (gamepadEnabled_) {
        pollGamepad(state);
    }
    if (keyboardEnabled_ || mouseEnabled_) {
        InputState fromDesk;
        if (pollKeyboardAndMouse(fromDesk)) {
            mergeInto(state, fromDesk);
        }
    }

    // The wheel accumulates in the background, so it has to be drained every
    // poll whether or not it is being used. Notches only count while the
    // modifier is held; otherwise scrolling a web page would reel the hand
    // in and out.
    const double notches = wheel_.consume();
    if (mouseEnabled_ && (modifierHeld() || directMode_)) {
        state.distanceImpulseMeters =
            static_cast<float>(notches) * wheelStepMeters_;
    }

    logChanges(state);
    return state;
}

} // namespace dk2vr
