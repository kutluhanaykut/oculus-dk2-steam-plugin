// Entry point vrserver calls to obtain the driver's interfaces.
#include <cstring>

#include "openvr_driver.h"

#include "device_provider.h"

#if defined(_WIN32)
#define HMD_DLL_EXPORT extern "C" __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define HMD_DLL_EXPORT extern "C" __attribute__((visibility("default")))
#else
#error "Unsupported platform."
#endif

namespace {

// One provider for the life of the DLL. vrserver may ask for the interface
// more than once, and it never takes ownership of what it gets back, so
// handing out a new object each time would leak.
dk2vr::Dk2ServerDriver g_serverDriver;

} // namespace

HMD_DLL_EXPORT void* HmdDriverFactory(const char* pInterfaceName, int* pReturnCode)
{
    if (pInterfaceName != nullptr
        && std::strcmp(vr::IServerTrackedDeviceProvider_Version, pInterfaceName) == 0) {
        if (pReturnCode != nullptr) {
            *pReturnCode = vr::VRInitError_None;
        }
        return &g_serverDriver;
    }

    if (pReturnCode != nullptr) {
        *pReturnCode = vr::VRInitError_Init_InterfaceNotFound;
    }
    return nullptr;
}
