// DK2 IMU reader for the SteamVR driver.
#include "dk2_imu.h"

#include "driverlog.h"

#include <Windows.h>
#include <setupapi.h>
#include <winusb.h>
#include <usbiodef.h>
#include <hidsdi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

namespace dk2vr_driver {
namespace {

// {A5DCBF10-6530-11D2-901F-00C04FB951ED} is GUID_DEVINTERFACE_USB_DEVICE
const GUID kUsbDeviceInterfaceGuid = {
    0xA5DCBF10, 0x6530, 0x11D2, {0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED}};

constexpr std::uint16_t kOculusVendorId = 0x2833;
constexpr std::uint16_t kDk2ProductIds[] = {0x0021, 0x2021};
constexpr std::uint8_t kDk2ImuEndpoint = 0x81;

// Quaternion multiplication: result = a * b
void quatMultiply(double aw, double ax, double ay, double az,
                  double bw, double bx, double by, double bz,
                  double& rw, double& rx, double& ry, double& rz)
{
    rw = aw * bw - ax * bx - ay * by - az * bz;
    rx = aw * bx + ax * bw + ay * bz - az * by;
    ry = aw * by - ax * bz + ay * bw + az * bx;
    rz = aw * bz + ax * by - ay * bx + az * bw;
}

// Quaternion normalization
void quatNormalize(double& w, double& x, double& y, double& z)
{
    const double n = std::sqrt(w * w + x * x + y * y + z * z);
    if (n > 0.0) {
        w /= n;
        x /= n;
        y /= n;
        z /= n;
    }
}

// Quaternion conjugate
void quatConjugate(double w, double x, double y, double z,
                   double& rw, double& rx, double& ry, double& rz)
{
    rw = w;
    rx = -x;
    ry = -y;
    rz = -z;
}

} // namespace

Dk2Imu::Dk2Imu() = default;

Dk2Imu::~Dk2Imu()
{
    disconnect();
}

bool Dk2Imu::connect()
{
    if (connected_) {
        return true;
    }
    lastError_.clear();

    // Try libusb first (most reliable for DK2)
    if (connectLibusb()) {
        return true;
    }

    // Fall back to hidapi
    if (connectHidApi()) {
        return true;
    }

    if (lastError_.empty()) {
        lastError_ = "DK2 USB tracking device not found. Make sure the DK2 is connected "
            "and has the WinUSB or libusb-win32 driver installed.";
    }
    return false;
}

bool Dk2Imu::connectLibusb()
{
    libusb_context* context = nullptr;
    if (libusb_init(&context) != LIBUSB_SUCCESS) {
        DriverLog("DK2 IMU: libusb_init failed.");
        return false;
    }

    libusb_device** deviceList = nullptr;
    const ssize_t deviceCount = libusb_get_device_list(context, &deviceList);
    if (deviceCount < 0) {
        DriverLog("DK2 IMU: libusb_get_device_list failed.");
        libusb_exit(context);
        return false;
    }

    libusb_device_handle* foundHandle = nullptr;
    libusb_device* foundDevice = nullptr;
    int foundInterface = -1;

    for (ssize_t i = 0; i < deviceCount; ++i) {
        libusb_device* device = deviceList[i];
        libusb_device_descriptor descriptor {};
        if (libusb_get_device_descriptor(device, &descriptor) != LIBUSB_SUCCESS) {
            continue;
        }
        if (descriptor.idVendor != kOculusVendorId) {
            continue;
        }
        const bool productMatch = std::find(std::begin(kDk2ProductIds),
            std::end(kDk2ProductIds), descriptor.idProduct) != std::end(kDk2ProductIds);
        if (!productMatch) {
            continue;
        }

        DriverLog("DK2 IMU: Found Oculus device VID=0x%04X PID=0x%04X",
            descriptor.idVendor, descriptor.idProduct);

        libusb_device_handle* handle = nullptr;
        const int openResult = libusb_open(device, &handle);
        if (openResult != LIBUSB_SUCCESS) {
            DriverLog("DK2 IMU: libusb_open failed, error=%d (%s)",
                openResult, libusb_error_name(openResult));
            continue;
        }

        libusb_set_auto_detach_kernel_driver(handle, 1);

        libusb_config_descriptor* config = nullptr;
        if (libusb_get_active_config_descriptor(device, &config) == LIBUSB_SUCCESS && config != nullptr) {
            for (std::uint8_t iface = 0; iface < config->bNumInterfaces; ++iface) {
                const libusb_interface& interface = config->interface[iface];
                if (interface.num_altsetting == 0) {
                    continue;
                }
                const libusb_interface_descriptor& alt = interface.altsetting[0];
                bool hasImuEndpoint = false;
                for (std::uint8_t ep = 0; ep < alt.bNumEndpoints; ++ep) {
                    if (alt.endpoint[ep].bEndpointAddress == kDk2ImuEndpoint) {
                        hasImuEndpoint = true;
                        break;
                    }
                }
                if (!hasImuEndpoint) {
                    continue;
                }
                if (libusb_claim_interface(handle, iface) == LIBUSB_SUCCESS) {
                    foundHandle = handle;
                    foundDevice = device;
                    foundInterface = static_cast<int>(iface);
                    break;
                }
            }
            libusb_free_config_descriptor(config);
        }

        if (foundHandle == nullptr) {
            libusb_close(handle);
        } else {
            break;
        }
    }

    libusb_free_device_list(deviceList, 1);

    if (foundHandle == nullptr) {
        DriverLog("DK2 IMU: No DK2 device could be opened via libusb.");
        libusb_exit(context);
        return false;
    }

    libusbHandle_ = foundHandle;
    libusbContext_ = context;
    libusbInterface_ = foundInterface;
    DriverLog("DK2 IMU: DK2 opened via libusb, interface=%d", foundInterface);

    // Configure the DK2 sensor: set packet interval and keep-alive interval.
    {
        std::uint8_t config[7] {0x02, 0x00, 0x00, 0x0C, 0x00, 0x10, 0x27};
        const int result = libusb_control_transfer(libusbHandle_,
            static_cast<std::uint8_t>(LIBUSB_REQUEST_TYPE_CLASS)
                | static_cast<std::uint8_t>(LIBUSB_RECIPIENT_INTERFACE)
                | static_cast<std::uint8_t>(LIBUSB_ENDPOINT_OUT),
            0x09, 0x0302, static_cast<std::uint16_t>(libusbInterface_),
            config, sizeof(config), 1000);
        if (result < 0) {
            DriverLog("DK2 IMU: sensor config failed, error=%d (%s)",
                result, libusb_error_name(result));
        }
    }

    // Enable the DK2 LEDs (required for IMU data streaming).
    {
        std::uint8_t enableLeds[17] {
            0x0C, 0x00, 0x00, 0x00, 0x01, 0x00, 0x5E, 0x01,
            0x1A, 0x41, 0x00, 0x00, 0x7F, 0x00, 0x00, 0x00, 0x00};
        const int result = libusb_control_transfer(libusbHandle_,
            static_cast<std::uint8_t>(LIBUSB_REQUEST_TYPE_CLASS)
                | static_cast<std::uint8_t>(LIBUSB_RECIPIENT_INTERFACE)
                | static_cast<std::uint8_t>(LIBUSB_ENDPOINT_OUT),
            0x09, 0x020C, static_cast<std::uint16_t>(libusbInterface_),
            enableLeds, sizeof(enableLeds), 1000);
        if (result < 0) {
            DriverLog("DK2 IMU: LED enable failed, error=%d (%s)",
                result, libusb_error_name(result));
        }
    }

    connected_ = true;
    stopRequested_ = false;
    readerThread_ = std::thread(&Dk2Imu::readerLoop, this);
    return true;
}

bool Dk2Imu::connectHidApi()
{
    hid_device_info* devices = hid_enumerate(0, 0);
    if (devices == nullptr) {
        DriverLog("DK2 IMU: hid_enumerate returned empty.");
        return false;
    }

    hid_device_info* match = nullptr;
    for (hid_device_info* current = devices; current != nullptr; current = current->next) {
        if (current->vendor_id != kOculusVendorId) {
            continue;
        }
        if (std::find(std::begin(kDk2ProductIds), std::end(kDk2ProductIds),
            current->product_id) == std::end(kDk2ProductIds)) {
            continue;
        }
        match = current;
        break;
    }

    if (match == nullptr) {
        DriverLog("DK2 IMU: No Oculus HID device found.");
        hid_free_enumeration(devices);
        return false;
    }

    hid_device* handle = hid_open_path(match->path);
    hid_free_enumeration(devices);
    if (handle == nullptr) {
        DriverLog("DK2 IMU: hid_open_path failed.");
        return false;
    }

    hidHandle_ = handle;
    DriverLog("DK2 IMU: DK2 opened via hidapi.");
    connected_ = true;
    stopRequested_ = false;
    readerThread_ = std::thread(&Dk2Imu::readerLoop, this);
    return true;
}

void Dk2Imu::disconnect()
{
    stopRequested_ = true;
    if (readerThread_.joinable()) {
        readerThread_.join();
    }
    if (hidHandle_ != nullptr) {
        hid_close(hidHandle_);
        hidHandle_ = nullptr;
    }
    if (libusbHandle_ != nullptr) {
        if (libusbInterface_ >= 0) {
            libusb_release_interface(libusbHandle_, libusbInterface_);
        }
        libusb_close(libusbHandle_);
        libusbHandle_ = nullptr;
    }
    if (libusbContext_ != nullptr) {
        libusb_exit(libusbContext_);
        libusbContext_ = nullptr;
    }
    connected_ = false;
}

bool Dk2Imu::isConnected() const noexcept
{
    return connected_;
}

const std::string& Dk2Imu::lastError() const noexcept
{
    return lastError_;
}

void Dk2Imu::recenter()
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    // Set calibration to the inverse of the current orientation
    quatConjugate(orientationW_, orientationX_, orientationY_, orientationZ_,
                  calibrationW_, calibrationX_, calibrationY_, calibrationZ_);
}

void Dk2Imu::getOrientation(double& w, double& x, double& y, double& z) const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    // Apply calibration: result = calibration * orientation
    quatMultiply(calibrationW_, calibrationX_, calibrationY_, calibrationZ_,
                 orientationW_, orientationX_, orientationY_, orientationZ_,
                 w, x, y, z);
    quatNormalize(w, x, y, z);
}

void Dk2Imu::decodeRiftSample(const std::uint8_t* buffer, std::int32_t out[3])
{
    const std::int32_t x = (static_cast<std::int32_t>(buffer[0]) << 24)
        | (static_cast<std::int32_t>(buffer[1]) << 16)
        | ((static_cast<std::int32_t>(buffer[2]) & 0xF8) << 8);
    const std::int32_t y = ((static_cast<std::int32_t>(buffer[2]) & 0x07) << 29)
        | (static_cast<std::int32_t>(buffer[3]) << 21)
        | (static_cast<std::int32_t>(buffer[4]) << 13)
        | ((static_cast<std::int32_t>(buffer[5]) & 0xC0) << 5);
    const std::int32_t z = ((static_cast<std::int32_t>(buffer[5]) & 0x3F) << 26)
        | (static_cast<std::int32_t>(buffer[6]) << 18)
        | (static_cast<std::int32_t>(buffer[7]) << 10);
    out[0] = x >> 11;
    out[1] = y >> 11;
    out[2] = z >> 11;
}

void Dk2Imu::parseImuPacket(const std::uint8_t* data, const std::size_t size)
{
    // DK2 sends IMU data with report ID 0x01 (DK1 format) or 0x0B (DK2 format).
    if (size < 26 || (data[0] != 0x01 && data[0] != 0x0B)) {
        return;
    }

    std::uint8_t numSamples = 0;
    std::uint64_t timestamp = 0;
    const std::uint8_t* samplePtr = nullptr;

    if (data[0] == 0x0B) {
        // DK2 format
        numSamples = data[3];
        timestamp = static_cast<std::uint64_t>(data[8])
            | (static_cast<std::uint64_t>(data[9]) << 8)
            | (static_cast<std::uint64_t>(data[10]) << 16)
            | (static_cast<std::uint64_t>(data[11]) << 24);
        samplePtr = data + 12;
        if (numSamples > 2) {
            numSamples = 2;
        }
    } else {
        // DK1 format
        numSamples = data[1];
        timestamp = static_cast<std::uint64_t>(data[2])
            | (static_cast<std::uint64_t>(data[3]) << 8);
        timestamp *= 1000; // DK1 timestamps are in milliseconds -> microseconds
        samplePtr = data + 8;
        if (numSamples > 3) {
            numSamples = 3;
        }
    }

    if (numSamples == 0 || samplePtr == nullptr) {
        return;
    }

    // Use the first sample's gyro data for integration.
    std::int32_t accel[3] {0, 0, 0};
    std::int32_t gyro[3] {0, 0, 0};
    decodeRiftSample(samplePtr, accel);
    decodeRiftSample(samplePtr + 8, gyro);

    // Convert raw values to physical units (radians per second).
    const double gyroRadPerSec[3] = {
        static_cast<double>(gyro[0]) * 0.0001,
        static_cast<double>(gyro[1]) * 0.0001,
        static_cast<double>(gyro[2]) * 0.0001};

    std::lock_guard<std::mutex> lock(stateMutex_);

    // Integrate gyro angular velocity over time to update orientation.
    if (haveLastImuTimestamp_ && timestamp > lastImuTimestamp_) {
        const double dtSeconds = static_cast<double>(timestamp - lastImuTimestamp_) / 1000000.0;
        if (dtSeconds > 0.0 && dtSeconds < 0.5) {
            // Convert angular velocity to quaternion delta
            const double halfAngleX = gyroRadPerSec[0] * dtSeconds * 0.5;
            const double halfAngleY = gyroRadPerSec[1] * dtSeconds * 0.5;
            const double halfAngleZ = gyroRadPerSec[2] * dtSeconds * 0.5;
            const double halfAngleSq = halfAngleX * halfAngleX + halfAngleY * halfAngleY + halfAngleZ * halfAngleZ;
            double dw, dx, dy, dz;
            if (halfAngleSq > 0.0) {
                const double halfAngle = std::sqrt(halfAngleSq);
                const double s = std::sin(halfAngle) / halfAngle;
                dw = std::cos(halfAngle);
                dx = halfAngleX * s;
                dy = halfAngleY * s;
                dz = halfAngleZ * s;
            } else {
                dw = 1.0;
                dx = 0.0;
                dy = 0.0;
                dz = 0.0;
            }

            // orientation = orientation * delta
            double newW, newX, newY, newZ;
            quatMultiply(orientationW_, orientationX_, orientationY_, orientationZ_,
                         dw, dx, dy, dz,
                         newW, newX, newY, newZ);
            orientationW_ = newW;
            orientationX_ = newX;
            orientationY_ = newY;
            orientationZ_ = newZ;
            quatNormalize(orientationW_, orientationX_, orientationY_, orientationZ_);
        }
    }

    lastImuTimestamp_ = timestamp;
    haveLastImuTimestamp_ = true;
}

void Dk2Imu::readerLoop()
{
    std::uint8_t buffer[64] {};
    while (!stopRequested_) {
        // Send keep-alive to the DK2 sensor periodically (every 10 seconds).
        if (libusbHandle_ != nullptr) {
            const auto nowMs = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            if (lastKeepAliveMs_ == 0 || (nowMs - lastKeepAliveMs_) >= 10000) {
                std::uint8_t keepAlive[5] {0x08, 0x00, 0x00, 0x10, 0x27};
                const int result = libusb_control_transfer(libusbHandle_,
                    static_cast<std::uint8_t>(LIBUSB_REQUEST_TYPE_CLASS)
                        | static_cast<std::uint8_t>(LIBUSB_RECIPIENT_INTERFACE)
                        | static_cast<std::uint8_t>(LIBUSB_ENDPOINT_OUT),
                    0x09, 0x0308, static_cast<std::uint16_t>(libusbInterface_),
                    keepAlive, sizeof(keepAlive), 1000);
                if (result < 0) {
                    DriverLog("DK2 IMU: keep-alive failed, error=%d", result);
                }
                lastKeepAliveMs_ = nowMs;
            }
        }

        if (libusbHandle_ != nullptr) {
            int transferred = 0;
            const int result = libusb_interrupt_transfer(libusbHandle_, kDk2ImuEndpoint,
                buffer, static_cast<int>(sizeof(buffer)), &transferred, 100);
            if (result == LIBUSB_SUCCESS && transferred > 0) {
                parseImuPacket(buffer, static_cast<std::size_t>(transferred));
            } else if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_TIMEOUT
                && result != LIBUSB_ERROR_INTERRUPTED) {
                DriverLog("DK2 IMU: libusb_interrupt_transfer failed, error=%d (%s)",
                    result, libusb_error_name(result));
                break;
            }
        } else if (hidHandle_ != nullptr) {
            const int bytesRead = hid_read(hidHandle_, buffer, sizeof(buffer));
            if (bytesRead > 0) {
                parseImuPacket(buffer, static_cast<std::size_t>(bytesRead));
            } else if (bytesRead < 0) {
                DriverLog("DK2 IMU: hid_read failed.");
                break;
            }
        } else {
            break;
        }

        Sleep(1);
    }
    DriverLog("DK2 IMU: reader loop ended.");
}

} // namespace dk2vr_driver
