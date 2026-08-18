//============ Copyright (c) Valve Corporation, All rights reserved. ============
#include "device_provider.h"

#include "driverlog.h"

Dk2ServerDriver::Dk2ServerDriver()
{
}

Dk2ServerDriver::~Dk2ServerDriver()
{
}

//-----------------------------------------------------------------------------
// Purpose: This is called by vrserver when the driver is loaded.
//-----------------------------------------------------------------------------
vr::EVRInitError Dk2ServerDriver::Init( vr::IVRDriverContext *pDriverContext )
{
	// Initialize the driver context. This will load the interfaces we need.
	VR_INIT_SERVER_DRIVER_CONTEXT( pDriverContext );

	// Create our HMD device driver
	hmd_driver_ = std::make_unique< Dk2HmdDeviceDriver >();

	// Add our HMD to the tracked device list
	vr::VRServerDriverHost()->TrackedDeviceAdded(
		hmd_driver_->MyGetSerialNumber().c_str(),
		vr::TrackedDeviceClass_HMD,
		hmd_driver_.get() );

	DriverLog( "DK2 VR Driver: HMD added to SteamVR." );

	// Connect to the DK2 IMU for rotational tracking
	imu_ = std::make_unique< dk2vr_driver::Dk2Imu >();
	if ( imu_->connect() )
	{
		DriverLog( "DK2 VR Driver: DK2 IMU connected successfully." );
	}
	else
	{
		DriverLog( "DK2 VR Driver: DK2 IMU connection failed: %s", imu_->lastError().c_str() );
		DriverLog( "DK2 VR Driver: Continuing without rotational tracking. The HMD will report a static orientation." );
	}

	return vr::VRInitError_None;
}

//-----------------------------------------------------------------------------
// Purpose: This is called by vrserver when the driver is unloaded.
//-----------------------------------------------------------------------------
void Dk2ServerDriver::Cleanup()
{
	// Disconnect from the DK2 IMU
	if ( imu_ )
	{
		imu_->disconnect();
		imu_.reset();
	}

	// Clean up our HMD driver
	hmd_driver_.reset();

	// Clean up the driver context
	VR_CLEANUP_SERVER_DRIVER_CONTEXT();
}


//-----------------------------------------------------------------------------
// Purpose: This is called by vrserver to get the interface versions this driver supports.
//-----------------------------------------------------------------------------
const char * const *Dk2ServerDriver::GetInterfaceVersions()
{
	return vr::k_InterfaceVersions;
}

//-----------------------------------------------------------------------------
// Purpose: This is called by vrserver every frame.
//-----------------------------------------------------------------------------
void Dk2ServerDriver::RunFrame()
{
	// Process any pending events
	vr::VREvent_t vrEvent;
	while ( vr::VRServerDriverHost()->PollNextEvent( &vrEvent, sizeof( vrEvent ) ) )
	{
		if ( hmd_driver_ )
		{
			hmd_driver_->MyProcessEvent( vrEvent );
		}
	}

	// Update the HMD orientation from the DK2 IMU
	if ( imu_ && imu_->isConnected() && hmd_driver_ )
	{
		double w, x, y, z;
		imu_->getOrientation( w, x, y, z );
		hmd_driver_->SetOrientation( w, x, y, z );
	}

	// Run the HMD driver's frame
	if ( hmd_driver_ )
	{
		hmd_driver_->MyRunFrame();
	}
}


//-----------------------------------------------------------------------------
// Purpose: This is called by vrserver when the system is about to enter standby mode.
//-----------------------------------------------------------------------------
bool Dk2ServerDriver::ShouldBlockStandbyMode()
{
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: This is called by vrserver when the system enters standby mode.
//-----------------------------------------------------------------------------
void Dk2ServerDriver::EnterStandby()
{
	DriverLog( "DK2 VR Driver: Entering standby mode." );
}

//-----------------------------------------------------------------------------
// Purpose: This is called by vrserver when the system leaves standby mode.
//-----------------------------------------------------------------------------
void Dk2ServerDriver::LeaveStandby()
{
	DriverLog( "DK2 VR Driver: Leaving standby mode." );
}
