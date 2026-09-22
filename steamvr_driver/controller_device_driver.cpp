#include "controller_device_driver.h"

#include "driverlog.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dk2vr {
namespace {

// Pretending to be a Vive wand means SteamVR's own bindings apply, so the
// dashboard laser and most games work without a custom input profile.
constexpr const char* kControllerType = "vive_controller";
constexpr const char* kInputProfilePath = "{htc}/input/vive_controller_profile.json";
constexpr const char* kRenderModel = "vr_controller_vive_1_5";

constexpr double kPi = 3.14159265358979323846;

Quat pitchQuaternion(const double radians)
{
    const double half = radians * 0.5;
    return Quat {std::cos(half), std::sin(half), 0.0, 0.0};
}

Quat yawQuaternion(const double radians)
{
    const double half = radians * 0.5;
    return Quat {std::cos(half), 0.0, std::sin(half), 0.0};
}

double degreesToRadians(const double degrees)
{
    return degrees * kPi / 180.0;
}

} // namespace

Dk2ControllerDeviceDriver::Dk2ControllerDeviceDriver(const DriverSettings& settings)
    : settings_(settings)
    , serialNumber_(settings.serialNumber + "_GAZE")
{
}

vr::EVRInitError Dk2ControllerDeviceDriver::Activate(const uint32_t unObjectId)
{
    deviceIndex_ = unObjectId;
    active_ = true;

    const vr::PropertyContainerHandle_t container =
        vr::VRProperties()->TrackedDeviceToPropertyContainer(unObjectId);

    vr::VRProperties()->SetStringProperty(container, vr::Prop_ModelNumber_String,
        "DK2 Gaze Pointer");
    vr::VRProperties()->SetStringProperty(container, vr::Prop_ManufacturerName_String, "Oculus");
    vr::VRProperties()->SetStringProperty(container, vr::Prop_SerialNumber_String,
        serialNumber_.c_str());
    vr::VRProperties()->SetStringProperty(container, vr::Prop_TrackingSystemName_String, "dk2vr");
    vr::VRProperties()->SetStringProperty(container, vr::Prop_RenderModelName_String,
        kRenderModel);
    vr::VRProperties()->SetStringProperty(container, vr::Prop_ControllerType_String,
        kControllerType);
    vr::VRProperties()->SetStringProperty(container, vr::Prop_InputProfilePath_String,
        kInputProfilePath);
    vr::VRProperties()->SetInt32Property(container, vr::Prop_ControllerRoleHint_Int32,
        vr::TrackedControllerRole_RightHand);
    vr::VRProperties()->SetInt32Property(container, vr::Prop_DeviceClass_Int32,
        vr::TrackedDeviceClass_Controller);
    vr::VRProperties()->SetBoolProperty(container, vr::Prop_DeviceIsWireless_Bool, false);
    vr::VRProperties()->SetBoolProperty(container, vr::Prop_DeviceProvidesBatteryStatus_Bool,
        false);
    vr::VRProperties()->SetBoolProperty(container, vr::Prop_NeverTracked_Bool, false);

    // The Vive wand's component layout. A component that fails to register is
    // silently dead at runtime, which looks exactly like "the button does
    // nothing", so every result is checked.
    const auto boolean = [container](const char* path, vr::VRInputComponentHandle_t* handle) {
        const vr::EVRInputError error =
            vr::VRDriverInput()->CreateBooleanComponent(container, path, handle);
        if (error != vr::VRInputError_None) {
            DriverLog("DK2 controller: failed to create %s (error %d).", path,
                static_cast<int>(error));
        }
    };
    const auto scalar = [container](const char* path, vr::VRInputComponentHandle_t* handle,
                            const vr::EVRScalarUnits units) {
        const vr::EVRInputError error = vr::VRDriverInput()->CreateScalarComponent(
            container, path, handle, vr::VRScalarType_Absolute, units);
        if (error != vr::VRInputError_None) {
            DriverLog("DK2 controller: failed to create %s (error %d).", path,
                static_cast<int>(error));
        }
    };

    boolean("/input/system/click", &systemClick_);
    boolean("/input/application_menu/click", &menuClick_);
    boolean("/input/grip/click", &gripClick_);
    boolean("/input/trigger/click", &triggerClick_);
    scalar("/input/trigger/value", &triggerValue_, vr::VRScalarUnits_NormalizedOneSided);
    boolean("/input/trackpad/click", &padClick_);
    boolean("/input/trackpad/touch", &padTouch_);
    scalar("/input/trackpad/x", &padX_, vr::VRScalarUnits_NormalizedTwoSided);
    scalar("/input/trackpad/y", &padY_, vr::VRScalarUnits_NormalizedTwoSided);
    // Declared so bindings that expect haptics resolve; there is nothing to
    // vibrate, and the events are ignored.
    vr::VRDriverInput()->CreateHapticComponent(container, "/output/haptic", &haptic_);

    DriverLog("DK2 controller: activated as device %u (gaze pointer).", unObjectId);
    return vr::VRInitError_None;
}

void* Dk2ControllerDeviceDriver::GetComponent(const char* /*pchComponentNameAndVersion*/)
{
    return nullptr;
}

void Dk2ControllerDeviceDriver::DebugRequest(const char* /*pchRequest*/,
    char* pchResponseBuffer, const uint32_t unResponseBufferSize)
{
    if (unResponseBufferSize >= 1) {
        pchResponseBuffer[0] = '\0';
    }
}

void Dk2ControllerDeviceDriver::setHeadOrientation(const Quat& orientation, const bool tracking)
{
    {
        std::lock_guard<std::mutex> lock(orientationMutex_);
        orientation_ = orientation;
    }
    tracking_ = tracking;
}

vr::DriverPose_t Dk2ControllerDeviceDriver::GetPose()
{
    vr::DriverPose_t pose {};
    pose.qWorldFromDriverRotation.w = 1.0;
    pose.qDriverFromHeadRotation.w = 1.0;

    Quat orientation;
    {
        std::lock_guard<std::mutex> lock(orientationMutex_);
        orientation = orientation_;
    }

    // The hand sits at an offset from the gaze direction, so it can be aimed
    // at something and left there instead of following the eyes everywhere.
    double aimYaw = 0.0;
    double aimPitch = 0.0;
    double distance = 0.35;
    {
        std::lock_guard<std::mutex> lock(orientationMutex_);
        aimYaw = aimYaw_;
        aimPitch = aimPitch_;
        distance = distance_;
    }

    const Quat aimOffset = multiply(yawQuaternion(aimYaw),
        pitchQuaternion(aimPitch + degreesToRadians(settings_.controllerPitchOffsetDegrees)));
    const Quat aimed = normalize(multiply(orientation, aimOffset));

    pose.qRotation.w = aimed.w;
    pose.qRotation.x = aimed.x;
    pose.qRotation.y = aimed.y;
    pose.qRotation.z = aimed.z;

    // Out along wherever the hand is aimed, dropped slightly so the rendered
    // wand does not sit in the middle of the view.
    const Vec3 offset = rotate(aimed, Vec3 {0.0, -0.05, -distance});
    pose.vecPosition[0] = offset.x;
    pose.vecPosition[1] = settings_.seatedHeightMeters + offset.y;
    pose.vecPosition[2] = offset.z;

    pose.deviceIsConnected = true;
    const bool tracking = tracking_;
    pose.poseIsValid = tracking;
    pose.result = tracking ? vr::TrackingResult_Running_OK
                           : vr::TrackingResult_Running_OutOfRange;
    pose.willDriftInYaw = true;

    return pose;
}

void Dk2ControllerDeviceDriver::update(const InputState& input)
{
    if (!active_) {
        return;
    }

    // Integrate the aim against real elapsed time, so the hand moves at the
    // same rate whether SteamVR is running the driver fast or slow.
    const auto now = std::chrono::steady_clock::now();
    double dt = 0.0;
    if (haveLastUpdate_) {
        dt = std::chrono::duration<double>(now - lastUpdate_).count();
        // A long stall (standby, a hitching game) should not teleport the hand.
        dt = std::clamp(dt, 0.0, 0.1);
    }
    lastUpdate_ = now;
    haveLastUpdate_ = true;

    {
        std::lock_guard<std::mutex> lock(orientationMutex_);
        if (input.resetAim) {
            aimYaw_ = 0.0;
            aimPitch_ = 0.0;
            distance_ = settings_.controllerDistanceMeters;
        } else if (dt > 0.0) {
            const double rate = degreesToRadians(settings_.aimSpeedDegreesPerSecond) * dt;
            aimYaw_ = std::clamp(aimYaw_ - input.aimX * rate,
                -degreesToRadians(settings_.aimLimitDegrees),
                degreesToRadians(settings_.aimLimitDegrees));
            aimPitch_ = std::clamp(aimPitch_ + input.aimY * rate,
                -degreesToRadians(settings_.aimLimitDegrees),
                degreesToRadians(settings_.aimLimitDegrees));
            distance_ = std::clamp(
                distance_ + input.distanceDelta * settings_.reachSpeedMetersPerSecond * dt
                    + input.distanceImpulseMeters,
                static_cast<double>(settings_.minDistanceMeters),
                static_cast<double>(settings_.maxDistanceMeters));
        }
    }

    vr::VRDriverInput()->UpdateBooleanComponent(systemClick_, input.systemClick, 0.0);
    vr::VRDriverInput()->UpdateBooleanComponent(menuClick_, input.menuClick, 0.0);
    vr::VRDriverInput()->UpdateBooleanComponent(gripClick_, input.gripClick, 0.0);
    vr::VRDriverInput()->UpdateBooleanComponent(triggerClick_, input.triggerClick, 0.0);
    vr::VRDriverInput()->UpdateScalarComponent(triggerValue_, input.triggerValue, 0.0);
    vr::VRDriverInput()->UpdateBooleanComponent(padClick_, input.padClick, 0.0);
    vr::VRDriverInput()->UpdateBooleanComponent(padTouch_, input.padTouch, 0.0);
    vr::VRDriverInput()->UpdateScalarComponent(padX_, input.padX, 0.0);
    vr::VRDriverInput()->UpdateScalarComponent(padY_, input.padY, 0.0);

    vr::VRServerDriverHost()->TrackedDevicePoseUpdated(deviceIndex_, GetPose(),
        sizeof(vr::DriverPose_t));
}

void Dk2ControllerDeviceDriver::EnterStandby()
{
}

void Dk2ControllerDeviceDriver::Deactivate()
{
    active_ = false;
    deviceIndex_ = vr::k_unTrackedDeviceIndexInvalid;
    DriverLog("DK2 controller: deactivated.");
}

} // namespace dk2vr
