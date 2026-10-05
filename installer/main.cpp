// DK2 SteamVR Driver Installer
//
// Locates the SteamVR installation, copies the DK2 driver files
// (driver_dk2vr.dll + driver.vrdrivermanifest) into SteamVR's drivers
// directory, and then runs a set of post-install sanity checks so the
// user can be confident the driver is ready before launching SteamVR.
//
// Exit codes:
//   0  - install + all tests passed
//   1  - install succeeded but one or more tests failed
//   2  - install failed (SteamVR not found, driver files missing, etc.)

#include <Windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <libusb.h>
#include <hidapi.h>

#include "core/display_geometry.h"

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr std::uint16_t kOculusVendorId = 0x2833;
// The DK2 tracker. PID 0x2021 on the same vendor id is the USB hub inside the
// headset: matching it would report a DK2 whose tracker is missing.
constexpr std::uint16_t kDk2TrackerProductId = 0x0021;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

std::string wideToUtf8(const std::wstring& text)
{
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring utf8ToWide(const std::string& text)
{
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
        static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        result.data(), size);
    return result;
}

// Returns the directory containing the current executable.
fs::path executableDirectory()
{
    wchar_t buffer[MAX_PATH] {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return fs::current_path();
    }
    return fs::path(buffer).parent_path();
}

// Reads a UTF-8 file into a string. Returns false on failure.
bool readFile(const fs::path& path, std::string& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    out = ss.str();
    return true;
}

// ---------------------------------------------------------------------------
// Test result tracking
// ---------------------------------------------------------------------------

struct TestResult {
    std::string name;
    bool passed {false};
    std::string detail;
};

std::vector<TestResult> g_tests;

void recordTest(const std::string& name, bool passed, const std::string& detail)
{
    g_tests.push_back({name, passed, detail});
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << name;
    if (!detail.empty()) {
        std::cout << " - " << detail;
    }
    std::cout << "\n";
}

// ---------------------------------------------------------------------------
// SteamVR discovery
// ---------------------------------------------------------------------------

// Finds the SteamVR installation directory via the Steam registry key.
// Returns empty path if not found.
fs::path findSteamVrPath()
{
    // HKCU\Software\Valve\Steam -> SteamPath
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0,
            KEY_READ, &key) == ERROR_SUCCESS) {
        wchar_t steamPath[MAX_PATH] {};
        DWORD size = sizeof(steamPath);
        const LSTATUS status = RegQueryValueExW(key, L"SteamPath", nullptr, nullptr,
            reinterpret_cast<LPBYTE>(steamPath), &size);
        RegCloseKey(key);
        if (status == ERROR_SUCCESS && steamPath[0] != L'\0') {
            fs::path candidate = fs::path(steamPath) / L"steamapps" / L"common" / L"SteamVR";
            if (fs::is_directory(candidate)) {
                return candidate;
            }
        }
    }

    // Fallback to the default Steam install location.
    const fs::path fallback = L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR";
    if (fs::is_directory(fallback)) {
        return fallback;
    }
    return {};
}

// ---------------------------------------------------------------------------
// Driver file discovery
// ---------------------------------------------------------------------------

// Locates the built driver files relative to the installer executable.
// The installer is placed in <build>/bin/<Config>/ while the driver output
// lives in <build>/bin/drivers/dk2vr/, so we search both the executable
// directory and its parent. Expected layouts:
//   <exe_dir>/drivers/dk2vr/driver.vrdrivermanifest
//   <exe_dir>/../drivers/dk2vr/driver.vrdrivermanifest
//   <exe_dir>/drivers/dk2vr/bin/win64/driver_dk2vr.dll   (manifest layout)
//   <exe_dir>/drivers/dk2vr/bin/Release/driver_dk2vr.dll (multi-config)
//   <exe_dir>/drivers/dk2vr/bin/driver_dk2vr.dll         (single-config fallback)
struct DriverFiles {
    fs::path dll;
    fs::path manifest;
    bool found {false};
    std::string error;
};

DriverFiles findDriverFiles()
{
    DriverFiles result;
    const fs::path exeDir = executableDirectory();

    // Candidate driver roots, in priority order.
    std::vector<fs::path> roots;
    roots.push_back(exeDir / L"drivers" / L"dk2vr");
    roots.push_back(exeDir.parent_path() / L"drivers" / L"dk2vr");

    for (const fs::path& driverRoot : roots) {
        // Manifest is always at the driver root.
        const fs::path manifest = driverRoot / L"driver.vrdrivermanifest";
        if (!fs::is_regular_file(manifest)) {
            continue;
        }

        // DLL may be in bin/win64 (manifest layout), bin/Release (multi-config),
        // or bin/ (single-config fallback).
        const fs::path dllWin64 = driverRoot / L"bin" / L"win64" / L"driver_dk2vr.dll";
        const fs::path dllMulti = driverRoot / L"bin" / L"Release" / L"driver_dk2vr.dll";
        const fs::path dllSingle = driverRoot / L"bin" / L"driver_dk2vr.dll";
        fs::path dll;
        if (fs::is_regular_file(dllWin64)) {
            dll = dllWin64;
        } else if (fs::is_regular_file(dllMulti)) {
            dll = dllMulti;
        } else if (fs::is_regular_file(dllSingle)) {
            dll = dllSingle;
        } else {
            continue;
        }

        result.dll = dll;
        result.manifest = manifest;
        result.found = true;
        return result;
    }

    result.error = "Surucu dosyalari bulunamadi. Beklenen konum: "
        + (exeDir / L"drivers" / L"dk2vr").string()
        + " veya " + (exeDir.parent_path() / L"drivers" / L"dk2vr").string();
    return result;
}

// ---------------------------------------------------------------------------
// DK2 USB / WinUSB detection
// ---------------------------------------------------------------------------

// Returns the Windows service bound to the DK2 tracker ("WinUSB", "HidUsb",
// ...), or an empty string if it is not plugged in. Read from the device's
// registry properties, so it works whatever driver is installed. (Zadig's
// WinUSB package does not register GUID_DEVINTERFACE_USB_DEVICE, so probing
// that interface class never finds the tracker.)
std::string trackerDriverService()
{
    HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(nullptr, L"USB", nullptr,
        DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        return {};
    }

    std::string service;
    SP_DEVINFO_DATA deviceInfo {};
    deviceInfo.cbSize = sizeof(deviceInfo);
    for (DWORD index = 0; SetupDiEnumDeviceInfo(deviceInfoSet, index, &deviceInfo); ++index) {
        wchar_t instanceId[MAX_DEVICE_ID_LEN] {};
        if (!SetupDiGetDeviceInstanceIdW(deviceInfoSet, &deviceInfo, instanceId,
                MAX_DEVICE_ID_LEN, nullptr)) {
            continue;
        }
        std::wstring id(instanceId);
        std::transform(id.begin(), id.end(), id.begin(),
            [](const wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
        if (id.find(L"USB\\VID_2833&PID_0021\\") != 0) {
            continue;
        }
        wchar_t value[256] {};
        if (SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfo, SPDRP_SERVICE,
                nullptr, reinterpret_cast<PBYTE>(value), sizeof(value) - sizeof(wchar_t),
                nullptr)) {
            service = wideToUtf8(value);
        } else {
            service = "(surucu yok)";
        }
        break;
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return service;
}

// Returns true if a DK2 tracking device is present at all (regardless of
// which driver it is bound to). Uses libusb enumeration.
bool isDk2Present()
{
    libusb_context* context = nullptr;
    if (libusb_init(&context) != LIBUSB_SUCCESS) {
        return false;
    }

    libusb_device** deviceList = nullptr;
    const ssize_t deviceCount = libusb_get_device_list(context, &deviceList);
    if (deviceCount < 0) {
        libusb_exit(context);
        return false;
    }

    bool found = false;
    for (ssize_t i = 0; i < deviceCount; ++i) {
        libusb_device_descriptor descriptor {};
        if (libusb_get_device_descriptor(deviceList[i], &descriptor) != LIBUSB_SUCCESS) {
            continue;
        }
        if (descriptor.idVendor != kOculusVendorId) {
            continue;
        }
        if (descriptor.idProduct == kDk2TrackerProductId) {
            found = true;
            break;
        }
    }

    libusb_free_device_list(deviceList, 1);
    libusb_exit(context);
    return found;
}

// ---------------------------------------------------------------------------
// IMU connection test
// ---------------------------------------------------------------------------

// Attempts to open the DK2 IMU and read a few orientation samples. Returns
// true if the device connects and produces valid data.
bool testImuConnection(std::string& detail)
{
    libusb_context* context = nullptr;
    if (libusb_init(&context) != LIBUSB_SUCCESS) {
        detail = "libusb baslatilamadi";
        return false;
    }

    libusb_device** deviceList = nullptr;
    const ssize_t deviceCount = libusb_get_device_list(context, &deviceList);
    if (deviceCount < 0) {
        detail = "USB cihaz listesi alinamadi";
        libusb_exit(context);
        return false;
    }

    libusb_device_handle* handle = nullptr;
    int interfaceIndex = -1;

    for (ssize_t i = 0; i < deviceCount; ++i) {
        libusb_device* device = deviceList[i];
        libusb_device_descriptor descriptor {};
        if (libusb_get_device_descriptor(device, &descriptor) != LIBUSB_SUCCESS) {
            continue;
        }
        if (descriptor.idVendor != kOculusVendorId) {
            continue;
        }
        if (descriptor.idProduct != kDk2TrackerProductId) {
            continue;
        }

        libusb_device_handle* candidate = nullptr;
        if (libusb_open(device, &candidate) != LIBUSB_SUCCESS) {
            continue;
        }
        libusb_set_auto_detach_kernel_driver(candidate, 1);

        libusb_config_descriptor* config = nullptr;
        if (libusb_get_active_config_descriptor(device, &config) == LIBUSB_SUCCESS
            && config != nullptr) {
            for (std::uint8_t iface = 0; iface < config->bNumInterfaces; ++iface) {
                const libusb_interface& interface = config->interface[iface];
                if (interface.num_altsetting == 0) {
                    continue;
                }
                const libusb_interface_descriptor& alt = interface.altsetting[0];
                bool hasImuEndpoint = false;
                for (std::uint8_t ep = 0; ep < alt.bNumEndpoints; ++ep) {
                    if (alt.endpoint[ep].bEndpointAddress == 0x81) {
                        hasImuEndpoint = true;
                        break;
                    }
                }
                if (!hasImuEndpoint) {
                    continue;
                }
                if (libusb_claim_interface(candidate, iface) == LIBUSB_SUCCESS) {
                    handle = candidate;
                    interfaceIndex = static_cast<int>(iface);
                    break;
                }
            }
            libusb_free_config_descriptor(config);
        }

        if (handle == nullptr) {
            libusb_close(candidate);
        } else {
            break;
        }
    }

    libusb_free_device_list(deviceList, 1);

    if (handle == nullptr) {
        detail = "DK2 IMU cihazi acilamadi (WinUSB/libusb surucusu gerekli)";
        libusb_exit(context);
        return false;
    }

    // Configure the sensor and enable LEDs (required for IMU streaming).
    {
        std::uint8_t config[7] {0x02, 0x00, 0x00, 0x0C, 0x00, 0x10, 0x27};
        libusb_control_transfer(handle,
            static_cast<std::uint8_t>(LIBUSB_REQUEST_TYPE_CLASS)
                | static_cast<std::uint8_t>(LIBUSB_RECIPIENT_INTERFACE)
                | static_cast<std::uint8_t>(LIBUSB_ENDPOINT_OUT),
            0x09, 0x0302, static_cast<std::uint16_t>(interfaceIndex),
            config, sizeof(config), 1000);
    }
    {
        std::uint8_t enableLeds[17] {
            0x0C, 0x00, 0x00, 0x00, 0x01, 0x00, 0x5E, 0x01,
            0x1A, 0x41, 0x00, 0x00, 0x7F, 0x00, 0x00, 0x00, 0x00};
        libusb_control_transfer(handle,
            static_cast<std::uint8_t>(LIBUSB_REQUEST_TYPE_CLASS)
                | static_cast<std::uint8_t>(LIBUSB_RECIPIENT_INTERFACE)
                | static_cast<std::uint8_t>(LIBUSB_ENDPOINT_OUT),
            0x09, 0x020C, static_cast<std::uint16_t>(interfaceIndex),
            enableLeds, sizeof(enableLeds), 1000);
    }

    // Read a few IMU packets to confirm data is flowing.
    bool gotData = false;
    std::uint8_t buffer[64] {};
    for (int attempt = 0; attempt < 20; ++attempt) {
        int transferred = 0;
        const int result = libusb_interrupt_transfer(handle, 0x81,
            buffer, static_cast<int>(sizeof(buffer)), &transferred, 100);
        if (result == LIBUSB_SUCCESS && transferred > 0) {
            gotData = true;
            break;
        }
        if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_TIMEOUT
            && result != LIBUSB_ERROR_INTERRUPTED) {
            break;
        }
    }

    libusb_release_interface(handle, static_cast<int>(interfaceIndex));
    libusb_close(handle);
    libusb_exit(context);

    if (gotData) {
        detail = "IMU verisi basariyla okundu";
        return true;
    }
    detail = "IMU verisi alinamadi (cihaz bagli ama veri akmadi)";
    return false;
}

// ---------------------------------------------------------------------------
// Manifest validation
// ---------------------------------------------------------------------------

// Performs a lightweight structural check of the driver manifest.
bool validateManifest(const fs::path& manifestPath, std::string& detail)
{
    std::string content;
    if (!readFile(manifestPath, content)) {
        detail = "manifest okunamadi";
        return false;
    }

    // The fields SteamVR actually needs to load the driver: a name, the path
    // to the DLL and the interface versions it was built against.
    const bool hasName = content.find("\"name\"") != std::string::npos;
    const bool hasDllPath = content.find("\"dllPath\"") != std::string::npos;
    const bool hasInterfaces = content.find("\"interfaceVersions\"") != std::string::npos;

    if (!hasName || !hasDllPath || !hasInterfaces) {
        detail = "manifest gerekli alanlari icermiyor (name / dllPath / interfaceVersions)";
        return false;
    }

    detail = "manifest gecerli";
    return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int runInstaller()
{
    std::cout << "============================================\n";
    std::cout << "  DK2 SteamVR Driver Installer\n";
    std::cout << "============================================\n\n";

    // 1. Find SteamVR.
    const fs::path steamVrPath = findSteamVrPath();
    if (steamVrPath.empty()) {
        std::cout << "[FAIL] SteamVR bulunamadi.\n";
        std::cout << "SteamVR'i Steam uzerinden kurun ve tekrar deneyin.\n";
        MessageBoxW(nullptr,
            L"SteamVR bulunamadi.\n\n"
            L"SteamVR'i Steam uzerinden kurun ve tekrar deneyin.",
            L"DK2 SteamVR Driver Installer", MB_OK | MB_ICONERROR);
        return 2;
    }
    std::cout << "[OK] SteamVR bulundu: " << steamVrPath.string() << "\n\n";

    // 2. Locate the built driver files.
    const DriverFiles driverFiles = findDriverFiles();
    if (!driverFiles.found) {
        std::cout << "[FAIL] Surucu dosyalari bulunamadi.\n";
        std::cout << "  " << driverFiles.error << "\n";
        std::cout << "Once projeyi derleyin (scripts/build.ps1).\n";
        MessageBoxW(nullptr,
            utf8ToWide("Surucu dosyalari bulunamadi.\n\n" + driverFiles.error
                + "\n\nOnce projeyi derleyin (scripts/build.ps1).").c_str(),
            L"DK2 SteamVR Driver Installer", MB_OK | MB_ICONERROR);
        return 2;
    }
    std::cout << "[OK] Surucu dosyalari bulundu:\n";
    std::cout << "  DLL:      " << driverFiles.dll.string() << "\n";
    std::cout << "  Manifest: " << driverFiles.manifest.string() << "\n\n";

    // 3. Copy the driver files into SteamVR.
    const fs::path destDir = steamVrPath / L"drivers" / L"dk2vr";
    const fs::path destBinDir = destDir / L"bin" / L"win64";
    std::error_code ec;
    fs::create_directories(destBinDir, ec);
    if (ec) {
        std::cout << "[FAIL] Surucu dizini olusturulamadi: " << ec.message() << "\n";
        MessageBoxW(nullptr,
            utf8ToWide("Surucu dizini olusturulamadi:\n" + ec.message()).c_str(),
            L"DK2 SteamVR Driver Installer", MB_OK | MB_ICONERROR);
        return 2;
    }

    const fs::path destDll = destBinDir / L"driver_dk2vr.dll";
    const fs::path destManifest = destDir / L"driver.vrdrivermanifest";

    fs::copy_file(driverFiles.dll, destDll, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cout << "[FAIL] driver_dk2vr.dll kopyalanamadi: " << ec.message() << "\n";
        MessageBoxW(nullptr,
            utf8ToWide("driver_dk2vr.dll kopyalanamadi:\n" + ec.message()).c_str(),
            L"DK2 SteamVR Driver Installer", MB_OK | MB_ICONERROR);
        return 2;
    }
    std::cout << "[OK] driver_dk2vr.dll kopyalandi -> " << destDll.string() << "\n";

    fs::copy_file(driverFiles.manifest, destManifest, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cout << "[FAIL] driver.vrdrivermanifest kopyalanamadi: " << ec.message() << "\n";
        MessageBoxW(nullptr,
            utf8ToWide("driver.vrdrivermanifest kopyalanamadi:\n" + ec.message()).c_str(),
            L"DK2 SteamVR Driver Installer", MB_OK | MB_ICONERROR);
        return 2;
    }
    std::cout << "[OK] driver.vrdrivermanifest kopyalandi -> " << destManifest.string() << "\n\n";

    // 4. Run post-install tests.
    std::cout << "--------------------------------------------\n";
    std::cout << "  Post-install testleri\n";
    std::cout << "--------------------------------------------\n";

    // Test: driver DLL exists at destination.
    recordTest("Surucu DLL kuruldu",
        fs::is_regular_file(destDll),
        fs::is_regular_file(destDll) ? destDll.string() : "dosya yok");

    // Test: manifest exists at destination.
    recordTest("Manifest kuruldu",
        fs::is_regular_file(destManifest),
        fs::is_regular_file(destManifest) ? destManifest.string() : "dosya yok");

    // Test: manifest is structurally valid.
    {
        std::string detail;
        const bool ok = validateManifest(destManifest, detail);
        recordTest("Manifest gecerli", ok, detail);
    }

    // Test: DK2 USB device present.
    {
        const bool present = isDk2Present();
        recordTest("DK2 USB cihazi algilandi", present,
            present ? "VID 2833 bulundu" : "DK2 USB bagli degil");
    }

    // Test: IMU connection (rotational tracking). This is the definitive check
    // that the DK2 is accessible through a usable driver (WinUSB or libusb-win32).
    bool imuOk = false;
    {
        std::string detail;
        imuOk = testImuConnection(detail);
        recordTest("IMU baglantisi (rotasyon takibi)", imuOk, detail);
    }

    // Test: the tracker is on a driver the IMU code can use. WinUSB is the
    // supported setup; anything else passes only if the IMU test above
    // actually got data through it.
    {
        const std::string service = trackerDriverService();
        const bool winUsb = _stricmp(service.c_str(), "WinUSB") == 0;
        std::string detail;
        if (service.empty()) {
            detail = "DK2 izleme cihazi (PID 0021) bulunamadi";
        } else if (winUsb) {
            detail = "WinUSB bagli";
        } else {
            detail = "Surucu: " + service
                + (imuOk ? " (IMU yine de erisilebilir)"
                         : " - Zadig ile izleme cihazina (PID 0021) WinUSB kurun");
        }
        recordTest("DK2 surucu baglantisi (WinUSB)", winUsb || imuOk, detail);
    }

    // Test: the DK2 panel is attached as an extended display. Without this
    // the compositor has nowhere to present, which is the usual cause of a
    // SteamVR session that starts but shows nothing in the headset.
    std::string displayDiagnostics;
    const dk2vr::DisplayInfo panel = dk2vr::findDk2Display(&displayDiagnostics);
    {
        std::string detail;
        if (panel.found) {
            detail = std::to_string(panel.width) + "x" + std::to_string(panel.height)
                + " @ " + std::to_string(panel.refreshHz) + "Hz, konum ("
                + std::to_string(panel.x) + ", " + std::to_string(panel.y) + ")";
        } else {
            detail = "OVR0003 ekrani masaustune bagli degil";
        }
        recordTest("DK2 HDMI ekrani algilandi", panel.found, detail);
    }

    // 5. Display report. Printed whether or not detection succeeded, because
    // the list of what Windows does see is the fastest way to work out why.
    std::cout << "\n--------------------------------------------\n";
    std::cout << "  Ekran raporu\n";
    std::cout << "--------------------------------------------\n";
    std::cout << displayDiagnostics;

    if (panel.found) {
        dk2vr::LayoutRequest request;
        request.x = panel.x;
        request.y = panel.y;
        request.width = panel.width;
        request.height = panel.height;
        const dk2vr::DisplayLayout layout = dk2vr::computeLayout(request);

        std::cout << "\nSurucunun kullanacagi yerlesim:\n";
        std::cout << "  Pencere:     (" << layout.windowX << ", " << layout.windowY
                  << ") " << layout.windowWidth << "x" << layout.windowHeight << "\n";
        std::cout << "  Goz duzeni:  " << dk2vr::toString(layout.split) << "\n";
        std::cout << "  Goruntu donusu: " << dk2vr::toString(layout.rotation)
                  << " (otomatik secildi)\n";
        std::cout << "  Goz basina render: " << layout.renderWidth << "x"
                  << layout.renderHeight << "\n";
        std::cout << "\nGozlukte goruntu yan dondukse, Steam/config/steamvr.vrsettings\n"
                     "icindeki \"dk2vr_display\" bolumune \"image_rotation\": 90 (ya da\n"
                     "180 / 270) ekleyip SteamVR'i yeniden baslatin.\n";
    } else {
        std::cout << "\nDK2 ekrani bulunamadi. HDMI kablosunu takin ve Windows ekran\n"
                     "ayarlarindan DK2'yi 'Genisletilmis' olarak ayarlayin.\n";
    }

    // 6. Summarize.
    std::cout << "\n--------------------------------------------\n";
    int passed = 0;
    int failed = 0;
    for (const TestResult& test : g_tests) {
        if (test.passed) {
            ++passed;
        } else {
            ++failed;
        }
    }
    std::cout << "Sonuc: " << passed << " gecti, " << failed << " basarisiz\n";
    std::cout << "--------------------------------------------\n";

    const bool allPassed = failed == 0;
    const std::wstring summary = utf8ToWide(
        std::string("DK2 SteamVR surucusu kuruldu.\n\n")
        + "Kurulum dizini: " + destDir.string() + "\n\n"
        + "Test sonuclari: " + std::to_string(passed) + " gecti, "
        + std::to_string(failed) + " basarisiz.\n\n"
        + (allPassed
            ? "Tum testler gecti. SteamVR'i yeniden baslatin ve "
              "Ayarlar > Gelistirici > DK2 VR Driver'i etkinlestirin."
            : "Bazi testler basarisiz. Yukaridaki ciktiyi inceleyin. "
              "DK2'nin USB baglantisi ve WinUSB surucusu kontrol edin."));

    MessageBoxW(nullptr, summary.c_str(),
        allPassed ? L"DK2 SteamVR Driver Installer - Basarili"
                  : L"DK2 SteamVR Driver Installer - Uyari",
        allPassed ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONWARNING);

    return allPassed ? 0 : 1;
}

} // namespace

int main()
{
    // Ensure the console uses UTF-8 for Turkish characters.
    SetConsoleOutputCP(CP_UTF8);

    try {
        return runInstaller();
    } catch (const std::exception& exception) {
        std::cout << "[FAIL] Beklenmeyen hata: " << exception.what() << "\n";
        MessageBoxW(nullptr, utf8ToWide(std::string("Beklenmeyen hata:\n") + exception.what()).c_str(),
            L"DK2 SteamVR Driver Installer", MB_OK | MB_ICONERROR);
        return 2;
    } catch (...) {
        std::cout << "[FAIL] Bilinmeyen hata.\n";
        MessageBoxW(nullptr, L"Bilinmeyen hata.", L"DK2 SteamVR Driver Installer",
            MB_OK | MB_ICONERROR);
        return 2;
    }
}
