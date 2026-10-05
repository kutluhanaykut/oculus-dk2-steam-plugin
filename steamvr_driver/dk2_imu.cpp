#include "dk2_imu.h"

#include "driverlog.h"

#include "core/imu_packet.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace dk2vr {
namespace {

constexpr std::uint16_t kOculusVendorId = 0x2833;
// The DK2 tracker. PID 0x2021 on the same vendor id is the USB hub inside
// the headset: it has no IMU and must never be opened or have its driver
// replaced, or the tracker behind it disappears.
constexpr std::uint16_t kDk2TrackerProductId = 0x0021;
constexpr std::uint8_t kDk2ImuEndpoint = 0x81;

constexpr std::uint64_t kKeepAliveIntervalMs = 3000;

// Class-specific SET_REPORT to the interface.
constexpr std::uint8_t kSetReportRequestType =
    static_cast<std::uint8_t>(LIBUSB_REQUEST_TYPE_CLASS)
    | static_cast<std::uint8_t>(LIBUSB_RECIPIENT_INTERFACE)
    | static_cast<std::uint8_t>(LIBUSB_ENDPOINT_OUT);
constexpr std::uint8_t kSetReportRequest = 0x09;

// Feature report 0x02: sensor config. Sets the sample rate divider and the
// keep-alive interval (0x2710 = 10000 ms) the tracker waits before it stops
// streaming on its own.
constexpr std::uint16_t kReportConfig = 0x0302;
// Feature report 0x08: keep-alive, resets that timer.
constexpr std::uint16_t kReportKeepAlive = 0x0308;
// Output report 0x0C: the DK2's tracking LEDs. The tracker only streams IMU
// data once this has been sent, even though no camera is in use.
constexpr std::uint16_t kReportLedControl = 0x020C;

std::uint64_t steadyMilliseconds()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

bool isDk2Product(const std::uint16_t productId)
{
    return productId == kDk2TrackerProductId;
}

// Payloads shared by the libusb and hidapi transports. The first byte is the
// report id, which is also what hidapi expects at the front of the buffer.
constexpr std::uint8_t kConfigPayload[7] {0x02, 0x00, 0x00, 0x0C, 0x00, 0x10, 0x27};
constexpr std::uint8_t kLedPayload[17] {
    0x0C, 0x00, 0x00, 0x00, 0x01, 0x00, 0x5E, 0x01,
    0x1A, 0x41, 0x00, 0x00, 0x7F, 0x00, 0x00, 0x00, 0x00};
constexpr std::uint8_t kKeepAlivePayload[5] {0x08, 0x00, 0x00, 0x10, 0x27};

} // namespace

Dk2Imu::Dk2Imu() = default;

Dk2Imu::~Dk2Imu()
{
    disconnect();
}

void Dk2Imu::setGravityGain(const double gain)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    gravityGain_ = gain;
    filter_.setGravityGain(gain);
}

bool Dk2Imu::connect()
{
    if (connected_) {
        return true;
    }
    // A previous reader thread may have exited on its own after a transport
    // error; make sure it is reaped before a new one is started.
    if (readerThread_.joinable()) {
        stopRequested_ = true;
        readerThread_.join();
    }
    closeTransports();
    stopRequested_ = false;
    lastError_.clear();

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        filter_.reset();
        filter_.setGravityGain(gravityGain_);
        haveLastTimestamp_ = false;
        lastTimestampMicros_ = 0;
    }
    packetsReceived_ = 0;
    packetsRejected_ = 0;

    if (connectLibusb()) {
        return true;
    }
    if (connectHidApi()) {
        return true;
    }

    if (lastError_.empty()) {
        lastError_ = "DK2 tracker not found. Check the USB cable, and make sure the "
                     "tracking device is bound to the WinUSB or libusb-win32 driver "
                     "(Oculus 0.8 runtime, or Zadig).";
    }
    return false;
}

bool Dk2Imu::connectLibusb()
{
    libusb_context* context = nullptr;
    if (libusb_init(&context) != LIBUSB_SUCCESS) {
        lastError_ = "libusb_init failed.";
        DriverLog("DK2 IMU: %s", lastError_.c_str());
        return false;
    }

    libusb_device** deviceList = nullptr;
    const ssize_t deviceCount = libusb_get_device_list(context, &deviceList);
    if (deviceCount < 0) {
        lastError_ = "libusb_get_device_list failed.";
        DriverLog("DK2 IMU: %s", lastError_.c_str());
        libusb_exit(context);
        return false;
    }

    libusb_device_handle* foundHandle = nullptr;
    int foundInterface = -1;
    bool sawDevice = false;

    for (ssize_t i = 0; i < deviceCount && foundHandle == nullptr; ++i) {
        libusb_device* device = deviceList[i];
        libusb_device_descriptor descriptor {};
        if (libusb_get_device_descriptor(device, &descriptor) != LIBUSB_SUCCESS) {
            continue;
        }
        if (descriptor.idVendor != kOculusVendorId || !isDk2Product(descriptor.idProduct)) {
            continue;
        }
        sawDevice = true;
        DriverLog("DK2 IMU: found Oculus device VID=0x%04X PID=0x%04X",
            descriptor.idVendor, descriptor.idProduct);

        libusb_device_handle* handle = nullptr;
        const int openResult = libusb_open(device, &handle);
        if (openResult != LIBUSB_SUCCESS) {
            lastError_ = std::string("The DK2 was found but could not be opened (")
                + libusb_error_name(openResult)
                + "). The tracking device is probably still bound to the Windows HID "
                  "driver; switch it to WinUSB with Zadig.";
            DriverLog("DK2 IMU: libusb_open failed, error=%d (%s)",
                openResult, libusb_error_name(openResult));
            continue;
        }

        libusb_set_auto_detach_kernel_driver(handle, 1);

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
                    foundInterface = static_cast<int>(iface);
                    break;
                }
            }
            libusb_free_config_descriptor(config);
        }

        if (foundHandle == nullptr) {
            libusb_close(handle);
        }
    }

    libusb_free_device_list(deviceList, 1);

    if (foundHandle == nullptr) {
        if (!sawDevice) {
            DriverLog("DK2 IMU: no Oculus VID 0x2833 device on the bus.");
        }
        libusb_exit(context);
        return false;
    }

    libusbHandle_ = foundHandle;
    libusbContext_ = context;
    libusbInterface_ = foundInterface;
    lastError_.clear();
    DriverLog("DK2 IMU: opened over libusb, interface=%d", foundInterface);

    const auto sendFeature = [this](const std::uint16_t report, const std::uint8_t* payload,
                                 const std::uint16_t length, const char* what) {
        // libusb takes a mutable buffer even for OUT transfers.
        std::uint8_t buffer[32] {};
        std::memcpy(buffer, payload, length);
        const int result = libusb_control_transfer(libusbHandle_, kSetReportRequestType,
            kSetReportRequest, report, static_cast<std::uint16_t>(libusbInterface_),
            buffer, length, 1000);
        if (result < 0) {
            DriverLog("DK2 IMU: %s failed, error=%d (%s)", what, result,
                libusb_error_name(result));
        }
        return result >= 0;
    };

    // Sample rate / keep-alive configuration.
    sendFeature(kReportConfig, kConfigPayload, sizeof(kConfigPayload), "sensor config");

    // Tracking LEDs. Required before the DK2 streams IMU reports.
    sendFeature(kReportLedControl, kLedPayload, sizeof(kLedPayload), "LED enable");

    transport_ = ImuTransport::Libusb;
    connected_ = true;
    readerThread_ = std::thread(&Dk2Imu::readerLoop, this);
    return true;
}

bool Dk2Imu::connectHidApi()
{
    hid_device_info* devices = hid_enumerate(kOculusVendorId, kDk2TrackerProductId);
    if (devices == nullptr) {
        DriverLog("DK2 IMU: hid_enumerate found no Oculus devices.");
        return false;
    }

    hid_device* handle = nullptr;
    for (hid_device_info* current = devices; current != nullptr; current = current->next) {
        if (current->vendor_id != kOculusVendorId || !isDk2Product(current->product_id)) {
            continue;
        }
        handle = hid_open_path(current->path);
        if (handle != nullptr) {
            break;
        }
        DriverLog("DK2 IMU: hid_open_path failed for a matching device.");
    }
    hid_free_enumeration(devices);

    if (handle == nullptr) {
        return false;
    }

    hidHandle_ = handle;
    // Blocking reads would stall the shutdown path; poll instead.
    hid_set_nonblocking(hidHandle_, 1);

    // Same start-up reports as the libusb path; without them the tracker
    // never starts streaming.
    if (hid_send_feature_report(hidHandle_, kConfigPayload, sizeof(kConfigPayload)) < 0) {
        DriverLog("DK2 IMU: sensor config failed over hidapi.");
    }
    if (hid_write(hidHandle_, kLedPayload, sizeof(kLedPayload)) < 0) {
        DriverLog("DK2 IMU: LED enable failed over hidapi.");
    }
    lastError_.clear();
    DriverLog("DK2 IMU: opened over hidapi.");
    transport_ = ImuTransport::HidApi;
    connected_ = true;
    readerThread_ = std::thread(&Dk2Imu::readerLoop, this);
    return true;
}

void Dk2Imu::closeTransports()
{
    if (hidHandle_ != nullptr) {
        hid_close(hidHandle_);
        hidHandle_ = nullptr;
    }
    if (libusbHandle_ != nullptr) {
        if (libusbInterface_ >= 0) {
            libusb_release_interface(libusbHandle_, libusbInterface_);
            libusbInterface_ = -1;
        }
        libusb_close(libusbHandle_);
        libusbHandle_ = nullptr;
    }
    if (libusbContext_ != nullptr) {
        libusb_exit(libusbContext_);
        libusbContext_ = nullptr;
    }
    transport_ = ImuTransport::None;
}

void Dk2Imu::disconnect()
{
    stopRequested_ = true;
    if (readerThread_.joinable()) {
        readerThread_.join();
    }
    closeTransports();
    connected_ = false;
}

bool Dk2Imu::isConnected() const noexcept
{
    return connected_;
}

ImuStatus Dk2Imu::status() const
{
    ImuStatus result;
    result.connected = connected_;
    result.transport = transport_;
    result.packetsReceived = packetsReceived_;
    result.packetsRejected = packetsRejected_;
    result.lastError = lastError_;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        result.gravityLocked = filter_.hasGravityLock();
    }
    return result;
}

void Dk2Imu::recenter()
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    filter_.recenter();
}

void Dk2Imu::setYawOffsetDegrees(const double degrees)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    filter_.setYawOffset(degrees * 3.14159265358979323846 / 180.0);
}

double Dk2Imu::yawOffsetDegrees() const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    return filter_.yawOffset() * 180.0 / 3.14159265358979323846;
}

Quat Dk2Imu::orientation() const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    return filter_.orientation();
}

void Dk2Imu::handlePacket(const std::uint8_t* data, const std::size_t size)
{
    const ImuPacket packet = parseImuPacket(data, size);
    if (!packet.valid) {
        ++packetsRejected_;
        return;
    }
    ++packetsReceived_;

    std::lock_guard<std::mutex> lock(stateMutex_);

    // The timestamp belongs to the last sample in the report, so the interval
    // covers all of them. Splitting it evenly integrates every sample instead
    // of throwing away all but the first.
    double totalSeconds = 0.0;
    if (haveLastTimestamp_ && packet.timestampMicros > lastTimestampMicros_) {
        totalSeconds = static_cast<double>(packet.timestampMicros - lastTimestampMicros_)
            / 1000000.0;
    }
    lastTimestampMicros_ = packet.timestampMicros;
    haveLastTimestamp_ = true;

    // A wrapped 32-bit counter, a dropped report or a stall all show up as an
    // implausible interval. Fall back to the nominal 1 kHz sample rate.
    if (!(totalSeconds > 0.0) || totalSeconds > 0.25) {
        totalSeconds = kSampleIntervalSeconds * packet.sampleCount;
    }

    const double dtPerSample = totalSeconds / packet.sampleCount;

    for (std::size_t index = 0; index < packet.sampleCount; ++index) {
        const ImuSample& sample = packet.samples[index];
        const Vec3 gyro {
            static_cast<double>(sample.gyro[0]) * kGyroScaleRadPerSecond,
            static_cast<double>(sample.gyro[1]) * kGyroScaleRadPerSecond,
            static_cast<double>(sample.gyro[2]) * kGyroScaleRadPerSecond};
        const Vec3 accel {
            static_cast<double>(sample.accel[0]) * kAccelScaleMetersPerSecond2,
            static_cast<double>(sample.accel[1]) * kAccelScaleMetersPerSecond2,
            static_cast<double>(sample.accel[2]) * kAccelScaleMetersPerSecond2};

        filter_.integrateGyro(gyro, dtPerSample);
        filter_.applyGravity(accel, dtPerSample);
    }
}

void Dk2Imu::readerLoop()
{
    std::uint8_t buffer[64] {};
    std::uint64_t lastKeepAliveMs = 0;
    std::uint64_t lastReportMs = steadyMilliseconds();

    while (!stopRequested_) {
        if (libusbHandle_ != nullptr) {
            const std::uint64_t nowMs = steadyMilliseconds();
            if (lastKeepAliveMs == 0 || (nowMs - lastKeepAliveMs) >= kKeepAliveIntervalMs) {
                std::uint8_t keepAlive[sizeof(kKeepAlivePayload)] {};
                std::memcpy(keepAlive, kKeepAlivePayload, sizeof(keepAlive));
                const int result = libusb_control_transfer(libusbHandle_,
                    kSetReportRequestType, kSetReportRequest, kReportKeepAlive,
                    static_cast<std::uint16_t>(libusbInterface_),
                    keepAlive, sizeof(keepAlive), 1000);
                if (result < 0) {
                    DriverLog("DK2 IMU: keep-alive failed, error=%d (%s)",
                        result, libusb_error_name(result));
                }
                lastKeepAliveMs = nowMs;
            }

            int transferred = 0;
            const int result = libusb_interrupt_transfer(libusbHandle_, kDk2ImuEndpoint,
                buffer, static_cast<int>(sizeof(buffer)), &transferred, 100);
            if (result == LIBUSB_SUCCESS && transferred > 0) {
                handlePacket(buffer, static_cast<std::size_t>(transferred));
                lastReportMs = nowMs;
            } else if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_TIMEOUT
                && result != LIBUSB_ERROR_INTERRUPTED) {
                lastError_ = std::string("USB transfer failed: ") + libusb_error_name(result);
                DriverLog("DK2 IMU: %s", lastError_.c_str());
                break;
            }
        } else if (hidHandle_ != nullptr) {
            const std::uint64_t nowMs = steadyMilliseconds();
            if (lastKeepAliveMs == 0 || (nowMs - lastKeepAliveMs) >= kKeepAliveIntervalMs) {
                if (hid_send_feature_report(hidHandle_, kKeepAlivePayload,
                        sizeof(kKeepAlivePayload)) < 0) {
                    DriverLog("DK2 IMU: keep-alive failed over hidapi.");
                }
                lastKeepAliveMs = nowMs;
            }

            const int bytesRead = hid_read_timeout(hidHandle_, buffer,
                sizeof(buffer), 100);
            if (bytesRead > 0) {
                handlePacket(buffer, static_cast<std::size_t>(bytesRead));
                lastReportMs = steadyMilliseconds();
            } else if (bytesRead < 0) {
                lastError_ = "hid_read failed; the DK2 was probably unplugged.";
                DriverLog("DK2 IMU: %s", lastError_.c_str());
                break;
            }
        } else {
            break;
        }

        // Surface a silent tracker once, rather than leaving the user staring
        // at a frozen view with nothing in the log.
        const std::uint64_t sinceReport = steadyMilliseconds() - lastReportMs;
        if (sinceReport > 5000 && packetsReceived_ == 0 && lastError_.empty()) {
            lastError_ = "The DK2 was opened but is not sending IMU reports. Try "
                         "unplugging and reconnecting the USB cable.";
            DriverLog("DK2 IMU: %s", lastError_.c_str());
        }
    }

    // Whatever brought us here, the transport is no longer delivering data.
    // Leaving connected_ set would make the HMD report a frozen orientation
    // for ever while claiming everything was fine.
    connected_ = false;
    DriverLog("DK2 IMU: reader loop ended after %llu packets (%llu rejected).",
        static_cast<unsigned long long>(packetsReceived_.load()),
        static_cast<unsigned long long>(packetsRejected_.load()));
}

} // namespace dk2vr
