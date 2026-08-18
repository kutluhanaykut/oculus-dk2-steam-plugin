//============ Copyright (c) Valve Corporation, All rights reserved. ============
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "openvr_driver.h"
#include "hmd_device_driver.h"
#include "dk2_imu.h"


//-----------------------------------------------------------------------------
// Purpose: The server tracked device provider is the entry point for the
// driver. It is responsible for enumerating and adding tracked devices.
//-----------------------------------------------------------------------------
class Dk2ServerDriver : public vr::IServerTrackedDeviceProvider
{
public:
	Dk2ServerDriver();
	virtual ~Dk2ServerDriver();

	vr::EVRInitError Init( vr::IVRDriverContext *pDriverContext ) override;
	void Cleanup() override;
	const char * const *GetInterfaceVersions() override;
	void RunFrame() override;
	bool ShouldBlockStandbyMode() override;
	void EnterStandby() override;
	void LeaveStandby() override;

private:
	std::unique_ptr< Dk2HmdDeviceDriver > hmd_driver_;
	std::unique_ptr< dk2vr_driver::Dk2Imu > imu_;
};


