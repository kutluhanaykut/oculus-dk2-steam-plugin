// Reads the Oculus DK2's inertial sensor and maintains a head orientation.
//
// The tracker is reached over libusb (WinUSB or libusb-win32 driver) with
// hidapi as a fallback. Packet decoding and sensor fusion live in core/ so
// they can be tested without hardware; this class is the transport, the
// reader thread and the DK2's feature-report handshake.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <hidapi.h>
#include <libusb.h>

#include "core/orientation_filter.h"

namespace dk2vr {

enum class ImuTransport {
    None,
    Libusb,
    HidApi,
};

struct ImuStatus {
    bool connected {false};
    ImuTransport transport {ImuTransport::None};
    std::uint64_t packetsReceived {0};
    std::uint64_t packetsRejected {0};
    bool gravityLocked {false};
    std::string lastError;
};

class Dk2Imu {
public:
    Dk2Imu();
    ~Dk2Imu();

    Dk2Imu(const Dk2Imu&) = delete;
    Dk2Imu& operator=(const Dk2Imu&) = delete;

    void setGravityGain(double gain);

    bool connect();
    void disconnect();

    bool isConnected() const noexcept;
    ImuStatus status() const;
    const std::string& lastError() const noexcept { return lastError_; }

    // Cancels yaw so the user faces forward. Pitch and roll are untouched.
    void recenter();

    // Manual "forward" correction, in degrees. Survives recentring.
    void setYawOffsetDegrees(double degrees);
    double yawOffsetDegrees() const;

    Quat orientation() const;

private:
    bool connectLibusb();
    bool connectHidApi();
    void closeTransports();
    void readerLoop();
    void handlePacket(const std::uint8_t* data, std::size_t size);

    hid_device* hidHandle_ {nullptr};
    libusb_device_handle* libusbHandle_ {nullptr};
    libusb_context* libusbContext_ {nullptr};
    int libusbInterface_ {-1};

    std::string lastError_;
    std::atomic<bool> connected_ {false};
    std::atomic<bool> stopRequested_ {false};
    std::atomic<ImuTransport> transport_ {ImuTransport::None};
    std::atomic<std::uint64_t> packetsReceived_ {0};
    std::atomic<std::uint64_t> packetsRejected_ {0};
    std::thread readerThread_;

    mutable std::mutex stateMutex_;
    OrientationFilter filter_;
    double gravityGain_ {1.0};
    std::uint64_t lastTimestampMicros_ {0};
    bool haveLastTimestamp_ {false};
};

} // namespace dk2vr
