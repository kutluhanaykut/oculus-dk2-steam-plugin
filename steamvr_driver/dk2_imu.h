// DK2 IMU reader for the SteamVR driver.
// Reads gyroscope data from the Oculus DK2 tracking device and
// integrates it to produce an orientation quaternion.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <hidapi.h>
#include <libusb.h>

namespace dk2vr_driver {

class Dk2Imu {
public:
    Dk2Imu();
    ~Dk2Imu();

    Dk2Imu(const Dk2Imu&) = delete;
    Dk2Imu& operator=(const Dk2Imu&) = delete;

    bool connect();
    void disconnect();
    bool isConnected() const noexcept;
    const std::string& lastError() const noexcept;

    void recenter();

    // Returns orientation as quaternion (w, x, y, z)
    void getOrientation(double& w, double& x, double& y, double& z) const;

private:
    bool connectLibusb();
    bool connectHidApi();
    void readerLoop();
    void parseImuPacket(const std::uint8_t* data, std::size_t size);
    static void decodeRiftSample(const std::uint8_t* buffer, std::int32_t out[3]);

    hid_device* hidHandle_ {nullptr};
    libusb_device_handle* libusbHandle_ {nullptr};
    libusb_context* libusbContext_ {nullptr};
    int libusbInterface_ {-1};

    std::string lastError_;
    std::atomic<bool> connected_ {false};
    std::atomic<bool> stopRequested_ {false};
    std::thread readerThread_;

    mutable std::mutex stateMutex_;
    double orientationW_ {1.0};
    double orientationX_ {0.0};
    double orientationY_ {0.0};
    double orientationZ_ {0.0};
    double calibrationW_ {1.0};
    double calibrationX_ {0.0};
    double calibrationY_ {0.0};
    double calibrationZ_ {0.0};

    std::uint64_t lastImuTimestamp_ {0};
    bool haveLastImuTimestamp_ {false};
    std::uint64_t lastKeepAliveMs_ {0};
};

} // namespace dk2vr_driver
