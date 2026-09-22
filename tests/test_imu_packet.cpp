#include "test_support.h"

#include "core/imu_packet.h"

#include <array>
#include <cstring>
#include <vector>

namespace dk2vr::tests {
namespace {

// Packs three 21-bit signed values the way the Rift does, so a decoded
// round-trip can be checked against known inputs.
void encodeRiftVector(const std::int32_t x, const std::int32_t y, const std::int32_t z,
    std::uint8_t* out)
{
    const auto ux = static_cast<std::uint32_t>(x) & 0x1FFFFFU;
    const auto uy = static_cast<std::uint32_t>(y) & 0x1FFFFFU;
    const auto uz = static_cast<std::uint32_t>(z) & 0x1FFFFFU;

    out[0] = static_cast<std::uint8_t>((ux >> 13) & 0xFF);
    out[1] = static_cast<std::uint8_t>((ux >> 5) & 0xFF);
    out[2] = static_cast<std::uint8_t>(((ux << 3) & 0xF8) | ((uy >> 18) & 0x07));
    out[3] = static_cast<std::uint8_t>((uy >> 10) & 0xFF);
    out[4] = static_cast<std::uint8_t>((uy >> 2) & 0xFF);
    out[5] = static_cast<std::uint8_t>(((uy << 6) & 0xC0) | ((uz >> 15) & 0x3F));
    out[6] = static_cast<std::uint8_t>((uz >> 7) & 0xFF);
    out[7] = static_cast<std::uint8_t>((uz << 1) & 0xFE);
}

std::vector<std::uint8_t> buildDk2Packet(const std::uint8_t sampleCount,
    const std::uint32_t timestampMicros, const std::int32_t gyroX)
{
    std::vector<std::uint8_t> packet(64, 0);
    packet[0] = 0x0B;
    packet[3] = sampleCount;
    packet[8] = static_cast<std::uint8_t>(timestampMicros & 0xFF);
    packet[9] = static_cast<std::uint8_t>((timestampMicros >> 8) & 0xFF);
    packet[10] = static_cast<std::uint8_t>((timestampMicros >> 16) & 0xFF);
    packet[11] = static_cast<std::uint8_t>((timestampMicros >> 24) & 0xFF);

    for (std::uint8_t index = 0; index < sampleCount && index < 2; ++index) {
        std::uint8_t* base = packet.data() + 12 + index * 16;
        encodeRiftVector(0, 98066, 0, base);            // accel: 9.81 m/s^2 up
        encodeRiftVector(gyroX, 0, 0, base + 8);        // gyro
    }
    return packet;
}

} // namespace

void runImuPacketTests()
{
    // --- bit packing round-trips, including negatives and the 21-bit edges ---
    {
        // 1048575 and -1048576 are the largest and smallest values the 21-bit
        // field can hold; getting the sign extension wrong shows up here as a
        // huge positive reading.
        const std::array<std::int32_t, 7> values {
            0, 1, -1, 12345, -12345, 1048575, -1048576};
        for (const std::int32_t value : values) {
            std::uint8_t buffer[8] {};
            encodeRiftVector(value, value, value, buffer);
            std::int32_t decoded[3] {};
            decodeRiftVector(buffer, decoded);
            CHECK(decoded[0] == value);
            CHECK(decoded[1] == value);
            CHECK(decoded[2] == value);
        }
    }

    // --- the three fields are independent, so a mix-up is visible ---
    {
        std::uint8_t buffer[8] {};
        encodeRiftVector(100, -200, 300, buffer);
        std::int32_t decoded[3] {};
        decodeRiftVector(buffer, decoded);
        CHECK(decoded[0] == 100);
        CHECK(decoded[1] == -200);
        CHECK(decoded[2] == 300);
    }

    // --- a well formed DK2 report ---
    {
        const std::vector<std::uint8_t> packet = buildDk2Packet(2, 123456, 5000);
        const ImuPacket parsed = parseImuPacket(packet.data(), packet.size());
        CHECK(parsed.valid);
        CHECK(parsed.isDk2Format);
        CHECK(parsed.sampleCount == 2);
        CHECK(parsed.timestampMicros == 123456U);
        CHECK(parsed.samples[0].gyro[0] == 5000);
        CHECK(parsed.samples[1].gyro[0] == 5000);
        CHECK(parsed.samples[0].accel[1] == 98066);
    }

    // --- a report that claims more samples than the transfer carried ---
    // This is the case that used to read past the end of the buffer: the
    // header plus one sample needs 28 bytes, not the 26 the old guard
    // allowed.
    {
        std::vector<std::uint8_t> packet = buildDk2Packet(2, 1000, 700);
        packet.resize(26); // one byte short of even a single complete sample
        const ImuPacket parsed = parseImuPacket(packet.data(), packet.size());
        CHECK(!parsed.valid);
        CHECK(parsed.sampleCount == 0);
    }
    {
        std::vector<std::uint8_t> packet = buildDk2Packet(2, 1000, 700);
        packet.resize(28); // exactly one complete sample
        const ImuPacket parsed = parseImuPacket(packet.data(), packet.size());
        CHECK(parsed.valid);
        CHECK(parsed.sampleCount == 1);
        CHECK(parsed.samples[0].gyro[0] == 700);
    }

    // --- a lying sample count is clamped to the format maximum ---
    {
        std::vector<std::uint8_t> packet = buildDk2Packet(2, 2000, 100);
        packet[3] = 200;
        const ImuPacket parsed = parseImuPacket(packet.data(), packet.size());
        CHECK(parsed.valid);
        CHECK(parsed.sampleCount == 2);
    }

    // --- DK1 reports are accepted and their timestamps normalised to us ---
    {
        std::vector<std::uint8_t> packet(62, 0);
        packet[0] = 0x01;
        packet[1] = 1;
        packet[2] = 0xE8; // 1000 ms
        packet[3] = 0x03;
        encodeRiftVector(0, 98066, 0, packet.data() + 8);
        encodeRiftVector(-1234, 0, 0, packet.data() + 16);

        const ImuPacket parsed = parseImuPacket(packet.data(), packet.size());
        CHECK(parsed.valid);
        CHECK(!parsed.isDk2Format);
        CHECK(parsed.sampleCount == 1);
        CHECK(parsed.timestampMicros == 1000000U);
        CHECK(parsed.samples[0].gyro[0] == -1234);
    }

    // --- anything that is not an IMU report is rejected, not decoded ---
    {
        std::vector<std::uint8_t> packet(64, 0);
        packet[0] = 0x02; // a feature report echo
        CHECK(!parseImuPacket(packet.data(), packet.size()).valid);

        CHECK(!parseImuPacket(nullptr, 64).valid);
        CHECK(!parseImuPacket(packet.data(), 0).valid);
        CHECK(!parseImuPacket(packet.data(), 1).valid);
    }

    // --- a report with zero samples carries no motion ---
    {
        const std::vector<std::uint8_t> packet = buildDk2Packet(0, 500, 0);
        CHECK(!parseImuPacket(packet.data(), packet.size()).valid);
    }
}

} // namespace dk2vr::tests
