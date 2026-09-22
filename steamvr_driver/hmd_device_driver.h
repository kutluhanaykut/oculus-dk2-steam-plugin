// The DK2 as a SteamVR tracked device.
//
// Rotation only: there is no positional tracking, so the HMD is reported at a
// fixed seated height and SteamVR is told the pose will drift in yaw.
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "openvr_driver.h"

#include "core/display_geometry.h"
#include "core/lens_distortion.h"
#include "core/orientation_filter.h"
#include "driver_settings.h"

namespace dk2vr {

// Everything the display component needs, resolved once at startup from the
// settings and from whatever Windows reports about the DK2 panel.
struct ResolvedDisplay {
    DisplayLayout layout {};
    bool autoDetected {false};
    float refreshHz {75.0F};
    std::int32_t renderWidth {960};
    std::int32_t renderHeight {1080};
    // Per-eye frustum tangents, already widened by the distortion.
    float tanOuter {1.1F};
    float tanInner {1.1F};
    float tanVertical {1.2F};
    DistortionCoefficients distortion {};
    bool distortionEnabled {true};
    float distortionScale {1.0F};
    float eyeAspect {0.889F};
    float lensCenterOffset {0.0F};
};

// Resolves the display configuration. Separated from the component so the
// installer can print the same summary without loading SteamVR.
ResolvedDisplay resolveDisplay(const DriverSettings& settings,
    const DisplayInfo& detected);

class Dk2DisplayComponent : public vr::IVRDisplayComponent {
public:
    explicit Dk2DisplayComponent(const ResolvedDisplay& resolved);

    bool IsDisplayOnDesktop() override;
    bool IsDisplayRealDisplay() override;
    void GetRecommendedRenderTargetSize(uint32_t* pnWidth, uint32_t* pnHeight) override;
    void GetEyeOutputViewport(vr::EVREye eEye, uint32_t* pnX, uint32_t* pnY,
        uint32_t* pnWidth, uint32_t* pnHeight) override;
    void GetProjectionRaw(vr::EVREye eEye, float* pfLeft, float* pfRight,
        float* pfTop, float* pfBottom) override;
    vr::DistortionCoordinates_t ComputeDistortion(vr::EVREye eEye, float fU, float fV) override;
    void GetWindowBounds(int32_t* pnX, int32_t* pnY, uint32_t* pnWidth,
        uint32_t* pnHeight) override;
    bool ComputeInverseDistortion(vr::HmdVector2_t* pResult, vr::EVREye eEye,
        uint32_t unChannel, float fU, float fV) override;

private:
    ResolvedDisplay resolved_;
};

class Dk2HmdDeviceDriver : public vr::ITrackedDeviceServerDriver {
public:
    Dk2HmdDeviceDriver(const DriverSettings& settings, const ResolvedDisplay& resolved);
    ~Dk2HmdDeviceDriver();

    vr::EVRInitError Activate(uint32_t unObjectId) override;
    void EnterStandby() override;
    void* GetComponent(const char* pchComponentNameAndVersion) override;
    void DebugRequest(const char* pchRequest, char* pchResponseBuffer,
        uint32_t unResponseBufferSize) override;
    vr::DriverPose_t GetPose() override;
    void Deactivate() override;

    const std::string& serialNumber() const noexcept { return serialNumber_; }

    // Called from the provider's frame with the latest IMU orientation. The
    // quaternion is stored under a lock so the pose thread never sees a
    // half-updated one.
    void setOrientation(const Quat& orientation);

    // Reported when the tracker is not delivering data, so SteamVR shows the
    // headset as connected but not tracking rather than silently frozen.
    void setTrackingValid(bool valid);

    // Keeps SteamVR's proximity sensor reading "worn". The DK2 has no such
    // sensor, and without one SteamVR decides the headset has been taken off
    // and drops the display into standby every half minute.
    void reportUserPresent();

    uint32_t deviceIndex() const noexcept { return deviceIndex_; }

private:
    void poseUpdateThread();

    DriverSettings settings_;
    ResolvedDisplay resolved_;
    std::unique_ptr<Dk2DisplayComponent> displayComponent_;

    std::string modelNumber_;
    std::string serialNumber_;

    std::atomic<bool> active_ {false};
    std::atomic<uint32_t> deviceIndex_ {vr::k_unTrackedDeviceIndexInvalid};
    std::atomic<bool> trackingValid_ {false};
    std::thread poseThread_;

    mutable std::mutex orientationMutex_;
    Quat orientation_ {};

    vr::VRInputComponentHandle_t proximity_ {vr::k_ulInvalidInputComponentHandle};
};

} // namespace dk2vr
