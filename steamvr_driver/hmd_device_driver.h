//============ Copyright (c) Valve Corporation, All rights reserved. ============
#pragma once

#include <array>
#include <string>
#include <memory>
#include <atomic>
#include <thread>

#include "openvr_driver.h"

// DK2 display configuration
struct Dk2DisplayDriverConfiguration
{
	int32_t window_x;
	int32_t window_y;

	int32_t window_width;
	int32_t window_height;

	int32_t render_width;
	int32_t render_height;
};

class Dk2DisplayComponent : public vr::IVRDisplayComponent
{
public:
	explicit Dk2DisplayComponent( const Dk2DisplayDriverConfiguration &config );

	// ----- Functions to override vr::IVRDisplayComponent -----
	bool IsDisplayOnDesktop() override;
	bool IsDisplayRealDisplay() override;
	void GetRecommendedRenderTargetSize( uint32_t *pnWidth, uint32_t *pnHeight ) override;
	void GetEyeOutputViewport( vr::EVREye eEye, uint32_t *pnX, uint32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight ) override;
	void GetProjectionRaw( vr::EVREye eEye, float *pfLeft, float *pfRight, float *pfTop, float *pfBottom ) override;
	vr::DistortionCoordinates_t ComputeDistortion( vr::EVREye eEye, float fU, float fV ) override;
	void GetWindowBounds( int32_t *pnX, int32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight ) override;
	bool ComputeInverseDistortion( vr::HmdVector2_t* pResult, vr::EVREye eEye, uint32_t unChannel, float fU, float fV) override;

private:
	Dk2DisplayDriverConfiguration config_;
};

//-----------------------------------------------------------------------------
// Purpose: Represents the DK2 HMD as a tracked device in SteamVR.
//-----------------------------------------------------------------------------
class Dk2HmdDeviceDriver : public vr::ITrackedDeviceServerDriver
{
public:
	Dk2HmdDeviceDriver();
	vr::EVRInitError Activate( uint32_t unObjectId ) override;
	void EnterStandby() override;
	void *GetComponent( const char *pchComponentNameAndVersion ) override;
	void DebugRequest( const char *pchRequest, char *pchResponseBuffer, uint32_t unResponseBufferSize ) override;
	vr::DriverPose_t GetPose() override;
	void Deactivate() override;

	// ----- Functions we declare ourselves below -----
	const std::string &MyGetSerialNumber();
	void MyRunFrame();
	void MyProcessEvent( const vr::VREvent_t &vrevent );
	void MyPoseUpdateThread();

	// Set the current orientation from the DK2 IMU (quaternion w,x,y,z)
	void SetOrientation( double w, double x, double y, double z );

private:
	std::unique_ptr< Dk2DisplayComponent > my_display_component_;

	std::string my_hmd_model_number_;
	std::string my_hmd_serial_number_;

	std::atomic< bool > is_active_;
	std::atomic< uint32_t > device_index_;

	std::thread my_pose_update_thread_;

	// Current orientation from DK2 IMU
	std::atomic< double > orientation_w_;
	std::atomic< double > orientation_x_;
	std::atomic< double > orientation_y_;
	std::atomic< double > orientation_z_;
};
