#include "device_provider.h"

#include "driverlog.h"

#include <chrono>
#include <sstream>

namespace dk2vr {
namespace {

constexpr std::uint64_t kReconnectIntervalMs = 3000;

std::uint64_t nowMs()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

// Writes a multi-line block one line at a time: the driver log wraps each
// call in its own timestamped entry and truncates at 1 KB.
void logBlock(const std::string& text)
{
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty()) {
            DriverLog("%s", line.c_str());
        }
    }
}

} // namespace

vr::EVRInitError Dk2ServerDriver::Init(vr::IVRDriverContext* pDriverContext)
{
    VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext);

    settings_ = loadDriverSettings();

    std::string displayDiagnostics;
    const DisplayInfo display = findDk2Display(&displayDiagnostics);
    const ResolvedDisplay resolved = resolveDisplay(settings_, display);

    logStartupDiagnostics(display, displayDiagnostics, resolved);

    hmd_ = std::make_unique<Dk2HmdDeviceDriver>(settings_, resolved);
    if (!vr::VRServerDriverHost()->TrackedDeviceAdded(hmd_->serialNumber().c_str(),
            vr::TrackedDeviceClass_HMD, hmd_.get())) {
        DriverLog("DK2: SteamVR refused to add the HMD.");
        return vr::VRInitError_Driver_Unknown;
    }

    if (settings_.controllerEnabled) {
        input_.configure(settings_.gamepadEnabled, settings_.keyboardEnabled,
            settings_.mouseEnabled, settings_.wheelStepMeters,
            settings_.trackpadRequiresModifier, settings_.directInputMode);
        controller_ = std::make_unique<Dk2ControllerDeviceDriver>(settings_);
        if (!vr::VRServerDriverHost()->TrackedDeviceAdded(controller_->serialNumber().c_str(),
                vr::TrackedDeviceClass_Controller, controller_.get())) {
            DriverLog("DK2: SteamVR refused to add the gaze controller; "
                      "pointing will not be available.");
            controller_.reset();
        }
    } else {
        DriverLog("DK2: gaze controller disabled by settings. Nothing will be able to "
                  "click in the SteamVR dashboard.");
    }

    imu_ = std::make_unique<Dk2Imu>();
    imu_->setGravityGain(settings_.gravityGain);
    yawOffsetDegrees_ = settings_.yawOffsetDegrees;
    imu_->setYawOffsetDegrees(yawOffsetDegrees_);
    if (yawOffsetDegrees_ != 0.0) {
        DriverLog("DK2: forward direction restored to %.1f degrees.", yawOffsetDegrees_);
    }
    if (imu_->connect()) {
        DriverLog("DK2: tracker connected; head orientation is live.");
        imuWasConnected_ = true;
    } else {
        DriverLog("DK2: tracker not connected: %s", imu_->lastError().c_str());
        DriverLog("DK2: the headset will show a fixed view until the tracker is "
                  "reachable. Reconnection is retried every few seconds.");
    }

    return vr::VRInitError_None;
}

void Dk2ServerDriver::logStartupDiagnostics(const DisplayInfo& display,
    const std::string& displayDiagnostics, const ResolvedDisplay& resolved) const
{
    DriverLog("DK2: ---- startup ----");
    DriverLog("DK2: settings");
    logBlock(describeSettings(settings_));

    DriverLog("DK2: displays seen by Windows");
    logBlock(displayDiagnostics);

    if (display.found) {
        DriverLog("DK2: panel found on %s at (%d, %d), %dx%d @ %d Hz, "
                  "Windows rotation %d deg.",
            display.monitorName.c_str(), display.x, display.y, display.width,
            display.height, display.refreshHz, display.windowsRotationDegrees);
    } else {
        DriverLog("DK2: no OVR0003 monitor is attached to the desktop. Falling back "
                  "to the configured window at (%d, %d), %dx%d.",
            settings_.fallbackWindowX, settings_.fallbackWindowY,
            settings_.fallbackWindowWidth, settings_.fallbackWindowHeight);
        DriverLog("DK2: connect the DK2 over HDMI and set it as an extended display "
                  "in Windows display settings.");
    }

    DriverLog("DK2: output window (%d, %d) %dx%d, eyes %s, image rotation %s%s",
        resolved.layout.windowX, resolved.layout.windowY,
        resolved.layout.windowWidth, resolved.layout.windowHeight,
        toString(resolved.layout.split), toString(resolved.layout.rotation),
        resolved.layout.swapEyes ? ", eyes swapped" : "");
    DriverLog("DK2: per-eye render target %dx%d, refresh %.1f Hz.",
        resolved.renderWidth, resolved.renderHeight,
        static_cast<double>(resolved.refreshHz));
    if (settings_.supersampleScale > 1.4F) {
        DriverLog("DK2: supersample is %.2f. SteamVR scales this again by its own "
                  "render resolution, and past about 1.4 the compositor has been "
                  "seen to fail and flood the panel with solid red. If the view is "
                  "red, lower \"supersample\" in dk2vr_display.",
            static_cast<double>(settings_.supersampleScale));
    }

    if (settings_.imageRotationDegrees == kRotationAuto) {
        if (display.found && display.windowsRotationDegrees != 0) {
            DriverLog("DK2: Windows is rotating this panel by %d degrees. That is "
                      "inherited rather than compensated for, because SteamVR "
                      "presents in the desktop's coordinate space.",
                display.windowsRotationDegrees);
        }
        DriverLog("DK2: image rotation was chosen automatically. If the view looks "
                  "turned on its side, set \"image_rotation\" to 0, 90, 180 or 270 "
                  "in the dk2vr_display section of steamvr.vrsettings.");
    }
    DriverLog("DK2: -----------------");
}

void Dk2ServerDriver::Cleanup()
{
    if (imu_) {
        imu_->disconnect();
        imu_.reset();
    }
    controller_.reset();
    hmd_.reset();
    VR_CLEANUP_SERVER_DRIVER_CONTEXT();
}

const char* const* Dk2ServerDriver::GetInterfaceVersions()
{
    return vr::k_InterfaceVersions;
}

void Dk2ServerDriver::processEvents()
{
    vr::VREvent_t event {};
    while (vr::VRServerDriverHost()->PollNextEvent(&event, sizeof(event))) {
        switch (event.eventType) {
        case vr::VREvent_SeatedZeroPoseReset:
        case vr::VREvent_StandingZeroPoseReset:
            // SteamVR's own "reset seated position" ends up here, which is
            // the natural place to cancel the accumulated yaw drift.
            if (imu_) {
                imu_->recenter();
                DriverLog("DK2: view recentred by SteamVR.");
            }
            break;
        default:
            break;
        }
    }
}

void Dk2ServerDriver::maintainImuConnection()
{
    if (!imu_) {
        return;
    }

    const bool connected = imu_->isConnected();
    if (connected) {
        if (!imuWasConnected_) {
            DriverLog("DK2: tracker reconnected.");
            imuWasConnected_ = true;
        }
        return;
    }

    if (imuWasConnected_) {
        DriverLog("DK2: tracker stopped responding: %s", imu_->lastError().c_str());
        imuWasConnected_ = false;
    }

    const std::uint64_t now = nowMs();
    if (now < nextReconnectMs_) {
        return;
    }
    nextReconnectMs_ = now + kReconnectIntervalMs;
    if (imu_->connect()) {
        DriverLog("DK2: tracker reconnected.");
        imuWasConnected_ = true;
    }
}

void Dk2ServerDriver::updateYawOffset(const float adjustRate)
{
    const std::uint64_t now = nowMs();
    const double dt = lastFrameMs_ == 0
        ? 0.0
        : (std::min)(static_cast<double>(now - lastFrameMs_) / 1000.0, 0.1);
    lastFrameMs_ = now;

    if (adjustRate != 0.0F && dt > 0.0 && imu_) {
        yawOffsetDegrees_ += static_cast<double>(adjustRate)
            * static_cast<double>(settings_.yawAdjustSpeedDegreesPerSecond) * dt;
        // Keep it readable in the settings file rather than winding up to
        // thousands of degrees.
        while (yawOffsetDegrees_ > 180.0) {
            yawOffsetDegrees_ -= 360.0;
        }
        while (yawOffsetDegrees_ < -180.0) {
            yawOffsetDegrees_ += 360.0;
        }
        imu_->setYawOffsetDegrees(yawOffsetDegrees_);
        yawOffsetDirty_ = true;
        // Debounced: writing on every frame would hammer the settings file
        // while the key is held.
        yawSaveDueMs_ = now + 1000;
    }

    if (yawOffsetDirty_ && now >= yawSaveDueMs_) {
        yawOffsetDirty_ = false;
        vr::VRSettings()->SetFloat("driver_dk2vr", "yaw_offset_degrees",
            static_cast<float>(yawOffsetDegrees_));
        DriverLog("DK2: forward direction saved at %.1f degrees.", yawOffsetDegrees_);
    }
}

void Dk2ServerDriver::RunFrame()
{
    processEvents();
    maintainImuConnection();

    const bool tracking = imu_ && imu_->isConnected();
    const Quat orientation = tracking ? imu_->orientation() : Quat {};

    if (hmd_) {
        hmd_->setOrientation(orientation);
        hmd_->setTrackingValid(tracking);
        // Re-asserted every frame: a single update at activation is not
        // enough to stop SteamVR deciding the headset has been taken off.
        hmd_->reportUserPresent();
    }

    if (controller_) {
        controller_->setHeadOrientation(orientation, tracking);
        const InputState input = input_.poll();
        if (input.recenterPressed && imu_) {
            // Recentring cancels drift but keeps the manual forward
            // correction, so it returns to the alignment that was set up
            // rather than to whatever direction the headset happens to face.
            imu_->recenter();
            DriverLog("DK2: view recentred (forward stays at %.1f degrees).",
                yawOffsetDegrees_);
        }
        updateYawOffset(input.yawAdjust);
        controller_->update(input);
    }
}

bool Dk2ServerDriver::ShouldBlockStandbyMode()
{
    return false;
}

void Dk2ServerDriver::EnterStandby()
{
    DriverLog("DK2: entering standby.");
}

void Dk2ServerDriver::LeaveStandby()
{
    DriverLog("DK2: leaving standby.");
}

} // namespace dk2vr
