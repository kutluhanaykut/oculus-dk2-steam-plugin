#include "hmd_device_driver.h"

#include "driverlog.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace dk2vr {
namespace {

// Time between the compositor handing a frame to the display and the photons
// leaving it. The DK2 scans out in about a frame.
constexpr float kSecondsFromVsyncToPhotons = 0.013F;

} // namespace

ResolvedDisplay resolveDisplay(const DriverSettings& settings, const DisplayInfo& detected)
{
    ResolvedDisplay resolved;
    resolved.autoDetected = detected.found;

    LayoutRequest request;
    if (detected.found) {
        request.x = detected.x;
        request.y = detected.y;
        request.width = detected.width;
        request.height = detected.height;
        request.windowsRotationDegrees = detected.windowsRotationDegrees;
        resolved.refreshHz = detected.refreshHz > 0
            ? static_cast<float>(detected.refreshHz)
            : 75.0F;
    } else {
        request.x = settings.fallbackWindowX;
        request.y = settings.fallbackWindowY;
        request.width = settings.fallbackWindowWidth;
        request.height = settings.fallbackWindowHeight;
        resolved.refreshHz = 75.0F;
    }
    request.rotationDegrees = settings.imageRotationDegrees;
    request.splitMode = settings.eyeSplitMode;
    request.swapEyes = settings.swapEyes;

    resolved.layout = computeLayout(request);

    if (settings.displayFrequencyOverride > 1.0F) {
        resolved.refreshHz = settings.displayFrequencyOverride;
    }

    const float supersample = settings.supersampleScale;
    resolved.renderWidth = (std::max)(
        static_cast<std::int32_t>(static_cast<float>(resolved.layout.renderWidth) * supersample),
        2);
    resolved.renderHeight = (std::max)(
        static_cast<std::int32_t>(static_cast<float>(resolved.layout.renderHeight) * supersample),
        2);

    resolved.distortion = settings.distortion;
    resolved.distortionEnabled = settings.distortionEnabled;
    resolved.distortionScale = fitScale(settings.distortion);
    resolved.lensCenterOffset = dk2vr::lensCenterOffset(settings.screenWidthMeters,
        settings.lensSeparationMeters);

    const float halfWidth = settings.screenWidthMeters * 0.5F;
    const float halfHeight = settings.screenHeightMeters * 0.5F;
    resolved.eyeAspect = halfHeight > 0.0001F ? halfWidth / halfHeight : 1.0F;

    // Frustum tangents from the physical geometry: how far the panel extends
    // to either side of a lens, over the distance from eye to panel. The
    // distortion magnifies the view, so the tangents are widened by the same
    // factor the warp uses.
    const float relief = settings.eyeReliefMeters > 0.005F ? settings.eyeReliefMeters : 0.041F;
    const float outerMeters = halfWidth - settings.lensSeparationMeters * 0.5F;
    const float innerMeters = settings.lensSeparationMeters * 0.5F;
    // Independent of the distortion toggle, so turning the warp off is a
    // clean A/B of the warp alone rather than also narrowing the view.
    const float magnify = settings.fovScale;

    resolved.tanOuter = (std::max)(outerMeters, 0.005F) / relief * magnify;
    resolved.tanInner = (std::max)(innerMeters, 0.005F) / relief * magnify;
    resolved.tanVertical = halfHeight / relief * magnify;

    return resolved;
}

// ---------------------------------------------------------------------------
// Display component
// ---------------------------------------------------------------------------

Dk2DisplayComponent::Dk2DisplayComponent(const ResolvedDisplay& resolved)
    : resolved_(resolved)
{
}

bool Dk2DisplayComponent::IsDisplayOnDesktop()
{
    // The DK2 is an extended desktop monitor, not a direct-mode display.
    return true;
}

bool Dk2DisplayComponent::IsDisplayRealDisplay()
{
    // Tells the compositor to find the panel through DXGI and present to it.
    // Returning false makes it render into a window instead, which shows up
    // as VRInitError_Compositor_InvalidOutputDesktop.
    return true;
}

void Dk2DisplayComponent::GetRecommendedRenderTargetSize(uint32_t* pnWidth, uint32_t* pnHeight)
{
    *pnWidth = static_cast<uint32_t>(resolved_.renderWidth);
    *pnHeight = static_cast<uint32_t>(resolved_.renderHeight);
}

void Dk2DisplayComponent::GetEyeOutputViewport(const vr::EVREye eEye, uint32_t* pnX,
    uint32_t* pnY, uint32_t* pnWidth, uint32_t* pnHeight)
{
    const EyeViewport viewport = eyeViewport(resolved_.layout, eEye == vr::Eye_Left);
    *pnX = viewport.x;
    *pnY = viewport.y;
    *pnWidth = viewport.width;
    *pnHeight = viewport.height;
}

void Dk2DisplayComponent::GetProjectionRaw(const vr::EVREye eEye, float* pfLeft,
    float* pfRight, float* pfTop, float* pfBottom)
{
    // The frustum is asymmetric: each lens sits closer to the middle of the
    // panel than to its outer edge.
    if (eEye == vr::Eye_Left) {
        *pfLeft = -resolved_.tanOuter;
        *pfRight = resolved_.tanInner;
    } else {
        *pfLeft = -resolved_.tanInner;
        *pfRight = resolved_.tanOuter;
    }
    *pfTop = -resolved_.tanVertical;
    *pfBottom = resolved_.tanVertical;
}

vr::DistortionCoordinates_t Dk2DisplayComponent::ComputeDistortion(const vr::EVREye eEye,
    const float fU, const float fV)
{
    // Undo the panel rotation first, so the distortion is applied in the same
    // upright frame the eye was rendered in.
    const Uv rotated = unrotate(resolved_.layout.rotation, Uv {fU, fV});

    vr::DistortionCoordinates_t coordinates {};
    if (!resolved_.distortionEnabled) {
        coordinates.rfRed[0] = rotated.u;
        coordinates.rfRed[1] = rotated.v;
        coordinates.rfGreen[0] = rotated.u;
        coordinates.rfGreen[1] = rotated.v;
        coordinates.rfBlue[0] = rotated.u;
        coordinates.rfBlue[1] = rotated.v;
        return coordinates;
    }

    EyeLensGeometry geometry;
    geometry.aspect = resolved_.eyeAspect;
    geometry.lensCenter.v = 0.5F;
    geometry.lensCenter.u = eEye == vr::Eye_Left
        ? 0.5F - resolved_.lensCenterOffset
        : 0.5F + resolved_.lensCenterOffset;

    const DistortionUv output {rotated.u, rotated.v};
    const DistortionUv red = applyBarrel(resolved_.distortion, geometry, output,
        ColorChannel::Red, resolved_.distortionScale);
    const DistortionUv green = applyBarrel(resolved_.distortion, geometry, output,
        ColorChannel::Green, resolved_.distortionScale);
    const DistortionUv blue = applyBarrel(resolved_.distortion, geometry, output,
        ColorChannel::Blue, resolved_.distortionScale);

    coordinates.rfRed[0] = red.u;
    coordinates.rfRed[1] = red.v;
    coordinates.rfGreen[0] = green.u;
    coordinates.rfGreen[1] = green.v;
    coordinates.rfBlue[0] = blue.u;
    coordinates.rfBlue[1] = blue.v;
    return coordinates;
}

void Dk2DisplayComponent::GetWindowBounds(int32_t* pnX, int32_t* pnY, uint32_t* pnWidth,
    uint32_t* pnHeight)
{
    *pnX = resolved_.layout.windowX;
    *pnY = resolved_.layout.windowY;
    *pnWidth = static_cast<uint32_t>(resolved_.layout.windowWidth);
    *pnHeight = static_cast<uint32_t>(resolved_.layout.windowHeight);
}

bool Dk2DisplayComponent::ComputeInverseDistortion(vr::HmdVector2_t* /*pResult*/,
    vr::EVREye /*eEye*/, uint32_t /*unChannel*/, float /*fU*/, float /*fV*/)
{
    // Returning false lets SteamVR estimate the inverse from ComputeDistortion.
    return false;
}

// ---------------------------------------------------------------------------
// HMD device
// ---------------------------------------------------------------------------

Dk2HmdDeviceDriver::Dk2HmdDeviceDriver(const DriverSettings& settings,
    const ResolvedDisplay& resolved)
    : settings_(settings)
    , resolved_(resolved)
    , modelNumber_(settings.modelNumber)
    , serialNumber_(settings.serialNumber)
{
    displayComponent_ = std::make_unique<Dk2DisplayComponent>(resolved_);
}

Dk2HmdDeviceDriver::~Dk2HmdDeviceDriver()
{
    // SteamVR normally calls Deactivate first, but a driver that is torn down
    // without it would otherwise destroy a running thread and terminate.
    if (active_.exchange(false) && poseThread_.joinable()) {
        poseThread_.join();
    }
}

vr::EVRInitError Dk2HmdDeviceDriver::Activate(const uint32_t unObjectId)
{
    deviceIndex_ = unObjectId;
    active_ = true;

    const vr::PropertyContainerHandle_t container =
        vr::VRProperties()->TrackedDeviceToPropertyContainer(unObjectId);

    vr::VRProperties()->SetStringProperty(container, vr::Prop_ModelNumber_String,
        modelNumber_.c_str());
    vr::VRProperties()->SetStringProperty(container, vr::Prop_ManufacturerName_String, "Oculus");
    vr::VRProperties()->SetStringProperty(container, vr::Prop_TrackingSystemName_String, "dk2vr");
    vr::VRProperties()->SetStringProperty(container, vr::Prop_SerialNumber_String,
        serialNumber_.c_str());

    const float ipd = vr::VRSettings()->GetFloat(vr::k_pch_SteamVR_Section,
        vr::k_pch_SteamVR_IPD_Float);
    vr::VRProperties()->SetFloatProperty(container, vr::Prop_UserIpdMeters_Float,
        ipd > 0.04F ? ipd : 0.064F);

    // The compositor refuses to start without a refresh rate.
    vr::VRProperties()->SetFloatProperty(container, vr::Prop_DisplayFrequency_Float,
        resolved_.refreshHz);
    vr::VRProperties()->SetFloatProperty(container, vr::Prop_UserHeadToEyeDepthMeters_Float, 0.F);
    vr::VRProperties()->SetFloatProperty(container,
        vr::Prop_SecondsFromVsyncToPhotons_Float, kSecondsFromVsyncToPhotons);

    vr::VRProperties()->SetBoolProperty(container, vr::Prop_IsOnDesktop_Bool, true);
    vr::VRProperties()->SetBoolProperty(container, vr::Prop_DisplayDebugMode_Bool, false);

    // No camera, so there is no positional tracking and yaw has no reference.
    vr::VRProperties()->SetBoolProperty(container, vr::Prop_WillDriftInYaw_Bool, true);
    vr::VRProperties()->SetFloatProperty(container,
        vr::Prop_TrackingRangeMinimumMeters_Float, 0.F);
    vr::VRProperties()->SetFloatProperty(container,
        vr::Prop_TrackingRangeMaximumMeters_Float, 0.F);

    // SteamVR watches a proximity sensor to know whether the headset is being
    // worn. The DK2 has none, so one is declared and held at "worn"; without
    // it SteamVR parks the display in standby a few seconds in and the view
    // goes blank.
    vr::VRProperties()->SetBoolProperty(container, vr::Prop_ContainsProximitySensor_Bool, true);
    const vr::EVRInputError proximityError =
        vr::VRDriverInput()->CreateBooleanComponent(container, "/proximity", &proximity_);
    if (proximityError != vr::VRInputError_None) {
        DriverLog("DK2 HMD: failed to create /proximity (error %d); SteamVR may drop "
                  "the display into standby.",
            static_cast<int>(proximityError));
    } else {
        vr::VRDriverInput()->UpdateBooleanComponent(proximity_, true, 0.0);
    }

    poseThread_ = std::thread(&Dk2HmdDeviceDriver::poseUpdateThread, this);

    DriverLog("DK2 HMD: activated as device %u (serial %s).", unObjectId,
        serialNumber_.c_str());
    return vr::VRInitError_None;
}

void Dk2HmdDeviceDriver::reportUserPresent()
{
    if (proximity_ != vr::k_ulInvalidInputComponentHandle) {
        vr::VRDriverInput()->UpdateBooleanComponent(proximity_, true, 0.0);
    }
}

void* Dk2HmdDeviceDriver::GetComponent(const char* pchComponentNameAndVersion)
{
    if (std::strcmp(pchComponentNameAndVersion, vr::IVRDisplayComponent_Version) == 0) {
        return displayComponent_.get();
    }
    return nullptr;
}

void Dk2HmdDeviceDriver::DebugRequest(const char* /*pchRequest*/, char* pchResponseBuffer,
    const uint32_t unResponseBufferSize)
{
    if (unResponseBufferSize >= 1) {
        pchResponseBuffer[0] = '\0';
    }
}

vr::DriverPose_t Dk2HmdDeviceDriver::GetPose()
{
    vr::DriverPose_t pose {};

    pose.qWorldFromDriverRotation.w = 1.0;
    pose.qDriverFromHeadRotation.w = 1.0;

    Quat orientation;
    {
        std::lock_guard<std::mutex> lock(orientationMutex_);
        orientation = orientation_;
    }
    pose.qRotation.w = orientation.w;
    pose.qRotation.x = orientation.x;
    pose.qRotation.y = orientation.y;
    pose.qRotation.z = orientation.z;

    // Rotation only: the head stays at a fixed seated height.
    pose.vecPosition[0] = 0.0;
    pose.vecPosition[1] = settings_.seatedHeightMeters;
    pose.vecPosition[2] = 0.0;

    pose.deviceIsConnected = true;
    const bool tracking = trackingValid_;
    pose.poseIsValid = tracking;
    pose.result = tracking ? vr::TrackingResult_Running_OK
                           : vr::TrackingResult_Running_OutOfRange;
    pose.shouldApplyHeadModel = true;
    pose.willDriftInYaw = true;

    return pose;
}

void Dk2HmdDeviceDriver::setOrientation(const Quat& orientation)
{
    std::lock_guard<std::mutex> lock(orientationMutex_);
    orientation_ = orientation;
}

void Dk2HmdDeviceDriver::setTrackingValid(const bool valid)
{
    trackingValid_ = valid;
}

void Dk2HmdDeviceDriver::poseUpdateThread()
{
    // Pace the updates to the panel rather than to an arbitrary millisecond
    // count, and use an absolute deadline so the rate does not sag under load.
    const double refresh = resolved_.refreshHz > 1.0F
        ? static_cast<double>(resolved_.refreshHz)
        : 75.0;
    const auto interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(1.0 / refresh));
    auto deadline = std::chrono::steady_clock::now();

    while (active_) {
        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(deviceIndex_, GetPose(),
            sizeof(vr::DriverPose_t));

        deadline += interval;
        const auto now = std::chrono::steady_clock::now();
        if (deadline < now) {
            deadline = now;
        }
        std::this_thread::sleep_until(deadline);
    }
}

void Dk2HmdDeviceDriver::EnterStandby()
{
    DriverLog("DK2 HMD: entering standby.");
}

void Dk2HmdDeviceDriver::Deactivate()
{
    if (active_.exchange(false) && poseThread_.joinable()) {
        poseThread_.join();
    }
    deviceIndex_ = vr::k_unTrackedDeviceIndexInvalid;
    DriverLog("DK2 HMD: deactivated.");
}

} // namespace dk2vr
