//============ Copyright (c) Valve Corporation, All rights reserved. ============
#include "hmd_device_driver.h"

#include "driverlog.h"
#include "vrmath.h"
#include <string.h>

// Settings sections
static const char *dk2_main_settings_section = "driver_dk2vr";
static const char *dk2_display_settings_section = "dk2vr_display";

Dk2HmdDeviceDriver::Dk2HmdDeviceDriver()
{
	// Keep track of whether Activate() has been called
	is_active_ = false;

	// Initialize orientation to identity
	orientation_w_ = 1.0;
	orientation_x_ = 0.0;
	orientation_y_ = 0.0;
	orientation_z_ = 0.0;

	// Get model number and serial number from SteamVR settings
	char model_number[ 1024 ];
	vr::VRSettings()->GetString( dk2_main_settings_section, "model_number", model_number, sizeof( model_number ) );
	my_hmd_model_number_ = model_number;

	char serial_number[ 1024 ];
	vr::VRSettings()->GetString( dk2_main_settings_section, "serial_number", serial_number, sizeof( serial_number ) );
	my_hmd_serial_number_ = serial_number;

	// SteamVR requires a non-empty serial number for tracked devices. If the
	// settings value is missing (e.g. manifest defaults not yet applied), fall
	// back to a stable identifier so the HMD is always registered.
	if ( my_hmd_serial_number_.empty() )
	{
		my_hmd_serial_number_ = "DK2VR0001";
		DriverLog( "DK2 VR Driver: Serial number empty, using fallback 'DK2VR0001'." );
	}

	DriverLog( "DK2 VR Driver: Model Number: %s", my_hmd_model_number_.c_str() );
	DriverLog( "DK2 VR Driver: Serial Number: %s", my_hmd_serial_number_.c_str() );

	// Display settings. The manifest declares defaults, but SteamVR may not
	// always apply them to steamvr.vrsettings, so fall back to DK2's native
	// 1920x1080 resolution when the settings are missing (GetInt32 returns 0).
	Dk2DisplayDriverConfiguration display_configuration{};
	display_configuration.window_x = vr::VRSettings()->GetInt32( dk2_display_settings_section, "window_x" );
	display_configuration.window_y = vr::VRSettings()->GetInt32( dk2_display_settings_section, "window_y" );

	display_configuration.window_width = vr::VRSettings()->GetInt32( dk2_display_settings_section, "window_width" );
	if ( display_configuration.window_width <= 0 )
	{
		display_configuration.window_width = 1920;
		DriverLog( "DK2 VR Driver: window_width not set, using default 1920." );
	}

	display_configuration.window_height = vr::VRSettings()->GetInt32( dk2_display_settings_section, "window_height" );
	if ( display_configuration.window_height <= 0 )
	{
		display_configuration.window_height = 1080;
		DriverLog( "DK2 VR Driver: window_height not set, using default 1080." );
	}

	display_configuration.render_width = vr::VRSettings()->GetInt32( dk2_display_settings_section, "render_width" );
	if ( display_configuration.render_width <= 0 )
	{
		display_configuration.render_width = 1920;
		DriverLog( "DK2 VR Driver: render_width not set, using default 1920." );
	}

	display_configuration.render_height = vr::VRSettings()->GetInt32( dk2_display_settings_section, "render_height" );
	if ( display_configuration.render_height <= 0 )
	{
		display_configuration.render_height = 1080;
		DriverLog( "DK2 VR Driver: render_height not set, using default 1080." );
	}

	// Instantiate our display component
	my_display_component_ = std::make_unique< Dk2DisplayComponent >( display_configuration );
}

//-----------------------------------------------------------------------------
// Purpose: This is called by vrserver after our
//  IServerTrackedDeviceProvider calls IVRServerDriverHost::TrackedDeviceAdded.
//-----------------------------------------------------------------------------
vr::EVRInitError Dk2HmdDeviceDriver::Activate( uint32_t unObjectId )
{
	// Let's keep track of our device index.
	device_index_ = unObjectId;

	// Set a member to keep track of whether we've activated yet or not
	is_active_ = true;

	// Properties are stored in containers, one per device index.
	vr::PropertyContainerHandle_t container = vr::VRProperties()->TrackedDeviceToPropertyContainer( device_index_ );

	// Set the model number.
	vr::VRProperties()->SetStringProperty( container, vr::Prop_ModelNumber_String, my_hmd_model_number_.c_str() );

	// Get the IPD of the user from SteamVR settings
	const float ipd = vr::VRSettings()->GetFloat( vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_IPD_Float );
	vr::VRProperties()->SetFloatProperty( container, vr::Prop_UserIpdMeters_Float, ipd );

	// For HMDs, it's required that a refresh rate is set otherwise VRCompositor will fail to start.
	// DK2 runs at 75Hz.
	vr::VRProperties()->SetFloatProperty( container, vr::Prop_DisplayFrequency_Float, 75.f );

	// The distance from the user's eyes to the display in meters. This is used for reprojection.
	vr::VRProperties()->SetFloatProperty( container, vr::Prop_UserHeadToEyeDepthMeters_Float, 0.f );

	// How long from the compositor to submit a frame to the time it takes to display it on the screen.
	vr::VRProperties()->SetFloatProperty( container, vr::Prop_SecondsFromVsyncToPhotons_Float, 0.11f );

	// The DK2 is used in extended desktop mode, so it IS on the desktop.
	vr::VRProperties()->SetBoolProperty( container, vr::Prop_IsOnDesktop_Bool, true );

	// Disable debug mode so the compositor renders normally to the HMD.
	vr::VRProperties()->SetBoolProperty( container, vr::Prop_DisplayDebugMode_Bool, false );

	// Set tracking system name
	vr::VRProperties()->SetStringProperty( container, vr::Prop_TrackingSystemName_String, "dk2vr" );

	// Set manufacturer name
	vr::VRProperties()->SetStringProperty( container, vr::Prop_ManufacturerName_String, "Oculus" );

	// Set tracking range
	vr::VRProperties()->SetInt32Property( container, vr::Prop_TrackingRangeMinimumMeters_Float, 0 );
	vr::VRProperties()->SetFloatProperty( container, vr::Prop_TrackingRangeMaximumMeters_Float, 0.f );

	// Set whether this HMD has positional tracking
	vr::VRProperties()->SetBoolProperty( container, vr::Prop_WillDriftInYaw_Bool, true );

	// Start the pose update thread
	my_pose_update_thread_ = std::thread( &Dk2HmdDeviceDriver::MyPoseUpdateThread, this );

	// We've activated everything successfully!
	return vr::VRInitError_None;
}

//-----------------------------------------------------------------------------
// Purpose: If you're an HMD, this is where you would return an implementation
// of vr::IVRDisplayComponent, vr::IVRVirtualDisplay or vr::IVRDirectModeComponent.
//-----------------------------------------------------------------------------
void *Dk2HmdDeviceDriver::GetComponent( const char *pchComponentNameAndVersion )
{
	if ( strcmp( pchComponentNameAndVersion, vr::IVRDisplayComponent_Version ) == 0 )
	{
		return my_display_component_.get();
	}

	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: This is called by vrserver when a debug request has been made from an application to the driver.
//-----------------------------------------------------------------------------
void Dk2HmdDeviceDriver::DebugRequest( const char *pchRequest, char *pchResponseBuffer, uint32_t unResponseBufferSize )
{
	if ( unResponseBufferSize >= 1 )
		pchResponseBuffer[ 0 ] = 0;
}

//-----------------------------------------------------------------------------
// Purpose: This is never called by vrserver in recent OpenVR versions,
// but is useful for giving data to vr::VRServerDriverHost::TrackedDevicePoseUpdated.
//-----------------------------------------------------------------------------
vr::DriverPose_t Dk2HmdDeviceDriver::GetPose()
{
	// Initialize the struct that we'll be submitting to the runtime.
	vr::DriverPose_t pose = { 0 };

	// These need to be set to be valid quaternions. The device won't appear otherwise.
	pose.qWorldFromDriverRotation.w = 1.f;
	pose.qDriverFromHeadRotation.w = 1.f;

	// Set the orientation from the DK2 IMU
	pose.qRotation.w = orientation_w_;
	pose.qRotation.x = orientation_x_;
	pose.qRotation.y = orientation_y_;
	pose.qRotation.z = orientation_z_;

	// Static position - the user is standing still (no positional tracking)
	// Position is at origin, 1.6m above the floor (typical standing height)
	pose.vecPosition[ 0 ] = 0.0f;
	pose.vecPosition[ 1 ] = 1.6f;
	pose.vecPosition[ 2 ] = 0.0f;

	// The pose we provided is valid.
	pose.poseIsValid = true;

	// Our device is always connected.
	pose.deviceIsConnected = true;

	// The state of our tracking.
	pose.result = vr::TrackingResult_Running_OK;

	// For HMDs we want to apply rotation/motion prediction
	pose.shouldApplyHeadModel = true;

	// We will drift in yaw (no positional tracking, only IMU)
	pose.willDriftInYaw = true;

	return pose;
}

void Dk2HmdDeviceDriver::MyPoseUpdateThread()
{
	while ( is_active_ )
	{
		// Inform the vrserver that our tracked device's pose has updated.
		vr::VRServerDriverHost()->TrackedDevicePoseUpdated( device_index_, GetPose(), sizeof( vr::DriverPose_t ) );

		// Update our pose every five milliseconds.
		std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: This is called by vrserver when the device should enter standby mode.
//-----------------------------------------------------------------------------
void Dk2HmdDeviceDriver::EnterStandby()
{
	DriverLog( "DK2 VR Driver: HMD has been put into standby." );
}

//-----------------------------------------------------------------------------
// Purpose: This is called by vrserver when the device should deactivate.
//-----------------------------------------------------------------------------
void Dk2HmdDeviceDriver::Deactivate()
{
	// Join our pose thread
	if ( is_active_.exchange( false ) )
	{
		my_pose_update_thread_.join();
	}

	// Unassign our device index
	device_index_ = vr::k_unTrackedDeviceIndexInvalid;
}

//-----------------------------------------------------------------------------
// Purpose: This is called by our IServerTrackedDeviceProvider when its RunFrame() method gets called.
//-----------------------------------------------------------------------------
void Dk2HmdDeviceDriver::MyRunFrame()
{
	// Nothing to do here - pose updates happen in the pose thread
}

//-----------------------------------------------------------------------------
// Purpose: This is called by our IServerTrackedDeviceProvider when it pops an event off the event queue.
//-----------------------------------------------------------------------------
void Dk2HmdDeviceDriver::MyProcessEvent( const vr::VREvent_t &vrevent )
{
}

//-----------------------------------------------------------------------------
// Purpose: Our IServerTrackedDeviceProvider needs our serial number to add us to vrserver.
//-----------------------------------------------------------------------------
const std::string &Dk2HmdDeviceDriver::MyGetSerialNumber()
{
	return my_hmd_serial_number_;
}

//-----------------------------------------------------------------------------
// Purpose: Set the current orientation from the DK2 IMU.
//-----------------------------------------------------------------------------
void Dk2HmdDeviceDriver::SetOrientation( double w, double x, double y, double z )
{
	orientation_w_ = w;
	orientation_x_ = x;
	orientation_y_ = y;
	orientation_z_ = z;
}

//-----------------------------------------------------------------------------
// DISPLAY DRIVER METHOD DEFINITIONS
//-----------------------------------------------------------------------------

// Windows display enumeration helpers (for locating the DK2 extended display)
#include <windows.h>

//-----------------------------------------------------------------------------
// Purpose: Find the on-screen position of the DK2 display by enumerating the
// Windows display devices and matching the Oculus Rift DK2 monitor (OVR0003).
// Returns true if found and fills in x/y. Otherwise returns false.
//-----------------------------------------------------------------------------
static bool FindDk2DisplayPosition( int32_t &out_x, int32_t &out_y )
{
	DISPLAY_DEVICEW dd{};
	dd.cb = sizeof( dd );

	for ( DWORD i = 0; EnumDisplayDevicesW( nullptr, i, &dd, 0 ); ++i )
	{
		// Look for the attached monitor for this display device
		DISPLAY_DEVICEW mon{};
		mon.cb = sizeof( mon );
		if ( EnumDisplayDevicesW( dd.DeviceName, 0, &mon, 0 ) )
		{
			// The DK2 monitor reports as "OVR0003" in its device ID
			if ( wcsstr( mon.DeviceID, L"OVR0003" ) != nullptr )
			{
				// Get the current display settings to find its position
				DEVMODEW dm{};
				dm.dmSize = sizeof( dm );
				if ( EnumDisplaySettingsW( dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm ) )
				{
					out_x = dm.dmPosition.x;
					out_y = dm.dmPosition.y;
					return true;
				}
			}
		}
		dd.cb = sizeof( dd );
	}

	return false;
}

Dk2DisplayComponent::Dk2DisplayComponent( const Dk2DisplayDriverConfiguration &config )
	: config_( config )
{
	// Auto-detect the DK2's on-screen position so SteamVR renders to the
	// correct extended display. This overrides the configured window position
	// when the DK2 monitor is found.
	int32_t dk2_x = 0, dk2_y = 0;
	if ( FindDk2DisplayPosition( dk2_x, dk2_y ) )
	{
		config_.window_x = dk2_x;
		config_.window_y = dk2_y;
		DriverLog( "DK2 VR Driver: DK2 display found at (%d, %d).", dk2_x, dk2_y );
	}
	else
	{
		DriverLog( "DK2 VR Driver: DK2 display not found, using configured window position (%d, %d).",
			config_.window_x, config_.window_y );
	}
}

//-----------------------------------------------------------------------------
// Purpose: To inform vrcompositor if this display is considered an on-desktop display.
//-----------------------------------------------------------------------------
bool Dk2DisplayComponent::IsDisplayOnDesktop()
{
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: To ask vrcompositor to search for this display.
// The DK2 is a real physical display (OVR0003) connected via HDMI in extended
// desktop mode. Returning true tells the compositor to find the display via
// DXGI and render to it directly, which is required for the compositor to
// present frames to the HMD. Returning false would make the compositor render
// to a window, which causes VRInitError_Compositor_InvalidOutputDesktop.
//-----------------------------------------------------------------------------
bool Dk2DisplayComponent::IsDisplayRealDisplay()
{
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: To inform the rest of the vr system what the recommended target size should be
//-----------------------------------------------------------------------------
void Dk2DisplayComponent::GetRecommendedRenderTargetSize( uint32_t *pnWidth, uint32_t *pnHeight )
{
	*pnWidth = config_.render_width;
	*pnHeight = config_.render_height;
}

//-----------------------------------------------------------------------------
// Purpose: To inform vrcompositor how the screens should be organized.
//-----------------------------------------------------------------------------
void Dk2DisplayComponent::GetEyeOutputViewport( vr::EVREye eEye, uint32_t *pnX, uint32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight )
{
	*pnY = 0;

	// Each eye will have half the window
	*pnWidth = config_.window_width / 2;

	// Each eye will have the full height
	*pnHeight = config_.window_height;

	if ( eEye == vr::Eye_Left )
	{
		// Left eye viewport on the left half of the window
		*pnX = 0;
	}
	else
	{
		// Right eye viewport on the right half of the window
		*pnX = config_.window_width / 2;
	}
}

//-----------------------------------------------------------------------------
// Purpose: To inform the compositor what the projection parameters are for this HMD.
// DK2 has a ~100 degree horizontal FOV.
//-----------------------------------------------------------------------------
void Dk2DisplayComponent::GetProjectionRaw( vr::EVREye eEye, float *pfLeft, float *pfRight, float *pfTop, float *pfBottom )
{
	// DK2 projection parameters (approximate)
	// Horizontal FOV ~100 degrees total, vertical ~90 degrees
	const float half_fov_h = 0.87f;  // ~50 degrees per eye
	const float half_fov_v = 0.78f;  // ~45 degrees

	if ( eEye == vr::Eye_Left )
	{
		*pfLeft = -half_fov_h;
		*pfRight = half_fov_h;
	}
	else
	{
		*pfLeft = -half_fov_h;
		*pfRight = half_fov_h;
	}
	*pfTop = -half_fov_v;
	*pfBottom = half_fov_v;
}

//-----------------------------------------------------------------------------
// Purpose: To compute the distortion properties for a given uv in an image.
// DK2 uses barrel distortion. We use identity distortion since SteamVR
// will handle the lens correction based on the projection parameters.
//-----------------------------------------------------------------------------
vr::DistortionCoordinates_t Dk2DisplayComponent::ComputeDistortion( vr::EVREye eEye, float fU, float fV )
{
	vr::DistortionCoordinates_t coordinates{};
	coordinates.rfBlue[ 0 ] = fU;
	coordinates.rfBlue[ 1 ] = fV;
	coordinates.rfGreen[ 0 ] = fU;
	coordinates.rfGreen[ 1 ] = fV;
	coordinates.rfRed[ 0 ] = fU;
	coordinates.rfRed[ 1 ] = fV;
	return coordinates;
}

//-----------------------------------------------------------------------------
// Purpose: To inform vrcompositor what the window bounds for this virtual HMD are.
//-----------------------------------------------------------------------------
void Dk2DisplayComponent::GetWindowBounds( int32_t *pnX, int32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight )
{
	*pnX = config_.window_x;
	*pnY = config_.window_y;
	*pnWidth = config_.window_width;
	*pnHeight = config_.window_height;
}

bool Dk2DisplayComponent::ComputeInverseDistortion(vr::HmdVector2_t* pResult, vr::EVREye eEye, uint32_t unChannel, float fU, float fV)
{
	// Return false to let SteamVR infer an estimate from ComputeDistortion
	return false;
}
