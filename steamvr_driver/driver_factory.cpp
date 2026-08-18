//============ Copyright (c) Valve Corporation, All rights reserved. ============
#include "openvr_driver.h"
#include "device_provider.h"

using namespace vr;

#if defined( _WIN32 )
#define HMD_DLL_EXPORT extern "C" __declspec( dllexport )
#define HMD_DLL_IMPORT extern "C" __declspec( dllimport )
#elif defined( __GNUC__ ) || defined( __clang__ )
#define HMD_DLL_EXPORT extern "C" __attribute__( ( visibility( "default" ) ) )
#define HMD_DLL_IMPORT extern "C"
#else
#error "Unsupported Platform."
#endif

//-----------------------------------------------------------------------------
// Purpose: This is the entry point for the driver. It is called by vrserver
// to get the driver factory.
//-----------------------------------------------------------------------------
HMD_DLL_EXPORT void *HmdDriverFactory( const char *pInterfaceName, int *pReturnCode )
{
	if ( 0 == strcmp( IServerTrackedDeviceProvider_Version, pInterfaceName ) )
	{
		return new Dk2ServerDriver;
	}

	if ( pReturnCode )
		*pReturnCode = VRInitError_Init_InterfaceNotFound;

	return NULL;
}
