#pragma once

#include <cstdint>
#include <memory>

#include "openvr_driver.h"

#include "controller_device_driver.h"
#include "dk2_imu.h"
#include "driver_settings.h"
#include "hmd_device_driver.h"
#include "input_source.h"

namespace dk2vr {

class Dk2ServerDriver : public vr::IServerTrackedDeviceProvider {
public:
    Dk2ServerDriver() = default;
    ~Dk2ServerDriver() = default;

    vr::EVRInitError Init(vr::IVRDriverContext* pDriverContext) override;
    void Cleanup() override;
    const char* const* GetInterfaceVersions() override;
    void RunFrame() override;
    bool ShouldBlockStandbyMode() override;
    void EnterStandby() override;
    void LeaveStandby() override;

private:
    void logStartupDiagnostics(const DisplayInfo& display,
        const std::string& displayDiagnostics, const ResolvedDisplay& resolved) const;
    void processEvents();
    void maintainImuConnection();
    // Turns the world by the requested rate, and writes the result back to
    // steamvr.vrsettings once it stops changing so the alignment only has to
    // be done once, not once per session.
    void updateYawOffset(float adjustRate);

    DriverSettings settings_ {};
    std::unique_ptr<Dk2HmdDeviceDriver> hmd_;
    std::unique_ptr<Dk2ControllerDeviceDriver> controller_;
    std::unique_ptr<Dk2Imu> imu_;
    InputSource input_ {};

    std::uint64_t nextReconnectMs_ {0};
    bool imuWasConnected_ {false};

    double yawOffsetDegrees_ {0.0};
    bool yawOffsetDirty_ {false};
    std::uint64_t yawSaveDueMs_ {0};
    std::uint64_t lastFrameMs_ {0};
};

} // namespace dk2vr
