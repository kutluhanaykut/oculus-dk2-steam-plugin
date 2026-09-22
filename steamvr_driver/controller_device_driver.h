// A virtual controller so the DK2 can be used on its own.
//
// Without a controller SteamVR has nothing to point with: the dashboard shows
// no laser, and menus cannot be clicked at all. This device sits a short
// distance in front of the user's eyes and points wherever they look, with
// its buttons driven by a gamepad or, as a fallback, the keyboard.
//
// It identifies itself as a Vive wand. That is deliberate: every game and the
// dashboard itself already ship bindings for that controller type, so the
// pointer works out of the box instead of needing a binding to be authored
// for a device nobody has heard of.
#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

#include "openvr_driver.h"

#include "core/orientation_filter.h"
#include "driver_settings.h"
#include "input_source.h"

namespace dk2vr {

class Dk2ControllerDeviceDriver : public vr::ITrackedDeviceServerDriver {
public:
    explicit Dk2ControllerDeviceDriver(const DriverSettings& settings);

    vr::EVRInitError Activate(uint32_t unObjectId) override;
    void EnterStandby() override;
    void* GetComponent(const char* pchComponentNameAndVersion) override;
    void DebugRequest(const char* pchRequest, char* pchResponseBuffer,
        uint32_t unResponseBufferSize) override;
    vr::DriverPose_t GetPose() override;
    void Deactivate() override;

    const std::string& serialNumber() const noexcept { return serialNumber_; }

    // Follows the head, so the pointer always aims where the user is looking.
    void setHeadOrientation(const Quat& orientation, bool tracking);

    // Pushes the latest button state into SteamVR's input system.
    void update(const InputState& input);

private:
    DriverSettings settings_;
    std::string serialNumber_;

    std::atomic<bool> active_ {false};
    std::atomic<uint32_t> deviceIndex_ {vr::k_unTrackedDeviceIndexInvalid};
    std::atomic<bool> tracking_ {false};

    mutable std::mutex orientationMutex_;
    Quat orientation_ {};

    // Where the hand is aimed relative to the head, and how far out it is.
    // Accumulated from the stick or mouse so the hand can be reached toward
    // something and left there, rather than snapping back to the gaze.
    double aimYaw_ {0.0};
    double aimPitch_ {0.0};
    double distance_ {0.35};
    std::chrono::steady_clock::time_point lastUpdate_ {};
    bool haveLastUpdate_ {false};

    vr::VRInputComponentHandle_t systemClick_ {vr::k_ulInvalidInputComponentHandle};
    vr::VRInputComponentHandle_t menuClick_ {vr::k_ulInvalidInputComponentHandle};
    vr::VRInputComponentHandle_t gripClick_ {vr::k_ulInvalidInputComponentHandle};
    vr::VRInputComponentHandle_t triggerClick_ {vr::k_ulInvalidInputComponentHandle};
    vr::VRInputComponentHandle_t triggerValue_ {vr::k_ulInvalidInputComponentHandle};
    vr::VRInputComponentHandle_t padClick_ {vr::k_ulInvalidInputComponentHandle};
    vr::VRInputComponentHandle_t padTouch_ {vr::k_ulInvalidInputComponentHandle};
    vr::VRInputComponentHandle_t padX_ {vr::k_ulInvalidInputComponentHandle};
    vr::VRInputComponentHandle_t padY_ {vr::k_ulInvalidInputComponentHandle};
    vr::VRInputComponentHandle_t haptic_ {vr::k_ulInvalidInputComponentHandle};
};

} // namespace dk2vr
