#include "test_support.h"

#include "core/orientation_filter.h"

#include <cmath>

namespace dk2vr::tests {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Angle between the filter's idea of "up" and true world up, in radians.
double tiltFromVertical(const OrientationFilter& filter)
{
    const Quat q = filter.rawOrientation();
    const Vec3 up = rotate(q, Vec3 {0.0, 1.0, 0.0});
    return std::atan2(std::sqrt(up.x * up.x + up.z * up.z), up.y);
}

} // namespace

void runOrientationTests()
{
    // --- quaternion helpers ---
    {
        const Quat identity;
        const Vec3 forward {0.0, 0.0, -1.0};
        const Vec3 same = rotate(identity, forward);
        CHECK_NEAR(same.z, -1.0, 1e-9);

        // 90 degrees about +Y takes -Z to -X.
        const double half = kPi / 4.0;
        const Quat yaw90 {std::cos(half), 0.0, std::sin(half), 0.0};
        const Vec3 turned = rotate(yaw90, forward);
        CHECK_NEAR(turned.x, -1.0, 1e-9);
        CHECK_NEAR(turned.z, 0.0, 1e-9);

        CHECK_NEAR(extractYaw(yaw90), kPi / 2.0, 1e-9);

        const Quat product = multiply(yaw90, conjugate(yaw90));
        CHECK_NEAR(product.w, 1.0, 1e-9);
    }

    // --- integrating a constant rate for a known time gives a known angle ---
    {
        OrientationFilter filter;
        filter.setGravityGain(0.0); // isolate the gyro path

        // One radian per second about +Y, for one second, in 1 ms steps.
        for (int step = 0; step < 1000; ++step) {
            filter.integrateGyro(Vec3 {0.0, 1.0, 0.0}, 0.001);
        }
        CHECK_NEAR(extractYaw(filter.rawOrientation()), 1.0, 1e-6);
    }

    // --- a fast turn keeps its magnitude ---
    // The small-angle shortcut the old code used loses length here, which
    // showed up as head turns that under-rotated.
    {
        OrientationFilter filter;
        filter.setGravityGain(0.0);
        // 10 rad/s for 100 ms in a single step is a full radian at once.
        filter.integrateGyro(Vec3 {0.0, 10.0, 0.0}, 0.1);
        CHECK_NEAR(extractYaw(filter.rawOrientation()), 1.0, 1e-9);
    }

    // --- gravity pulls an accumulated tilt back out ---
    {
        OrientationFilter filter;
        filter.setGravityGain(0.0);
        // Drift 0.2 rad of roll, as gyro bias would over a few minutes.
        filter.integrateGyro(Vec3 {0.0, 0.0, 0.2}, 1.0);
        const double tiltBefore = tiltFromVertical(filter);
        CHECK_NEAR(tiltBefore, 0.2, 1e-6);

        filter.setGravityGain(1.0);
        // A still headset reads 9.81 m/s^2 straight up its own axis.
        for (int step = 0; step < 5000; ++step) {
            filter.integrateGyro(Vec3 {0.0, 0.0, 0.0}, 0.001);
            filter.applyGravity(Vec3 {0.0, 9.80665, 0.0}, 0.001);
        }
        CHECK(tiltFromVertical(filter) < 0.01);
        CHECK(filter.hasGravityLock());
    }

    // --- gravity never touches yaw ---
    {
        OrientationFilter filter;
        filter.setGravityGain(1.0);
        filter.integrateGyro(Vec3 {0.0, 1.5, 0.0}, 1.0); // yaw only
        const double yawBefore = extractYaw(filter.rawOrientation());
        for (int step = 0; step < 2000; ++step) {
            filter.applyGravity(Vec3 {0.0, 9.80665, 0.0}, 0.001);
        }
        CHECK_NEAR(extractYaw(filter.rawOrientation()), yawBefore, 1e-6);
    }

    // --- a reading that is not gravity is ignored ---
    {
        OrientationFilter filter;
        filter.setGravityGain(1.0);
        filter.integrateGyro(Vec3 {0.0, 0.0, 0.3}, 1.0);
        const double before = tiltFromVertical(filter);
        for (int step = 0; step < 1000; ++step) {
            // 3 g sideways: the head is being shaken, not sitting still.
            filter.applyGravity(Vec3 {29.4, 0.0, 0.0}, 0.001);
        }
        CHECK_NEAR(tiltFromVertical(filter), before, 1e-9);
    }

    // --- recentring cancels yaw but leaves the horizon alone ---
    {
        OrientationFilter filter;
        filter.setGravityGain(0.0);
        filter.integrateGyro(Vec3 {0.0, 1.0, 0.0}, 1.0);  // yaw
        filter.integrateGyro(Vec3 {0.4, 0.0, 0.0}, 1.0);  // look down
        const double pitchTiltBefore = tiltFromVertical(filter);

        filter.recenter();
        const Quat after = filter.orientation();

        CHECK_NEAR(extractYaw(after), 0.0, 1e-6);
        // The pitch must survive: recentring while looking down should not
        // tip the world over.
        const Vec3 up = rotate(after, Vec3 {0.0, 1.0, 0.0});
        const double tiltAfter = std::atan2(std::sqrt(up.x * up.x + up.z * up.z), up.y);
        CHECK_NEAR(tiltAfter, pitchTiltBefore, 1e-6);
    }

    // --- the manual forward correction survives recentring ---
    // This is the whole point of it: a game decides its own forward, the user
    // lines the world up once, and recentring afterwards must return to that
    // alignment rather than throwing it away.
    {
        OrientationFilter filter;
        filter.setGravityGain(0.0);
        filter.setYawOffset(kPi / 2.0); // world turned a quarter turn

        filter.integrateGyro(Vec3 {0.0, 0.7, 0.0}, 1.0); // head turned somewhere
        filter.recenter();

        // Facing the calibrated forward, the report is the offset itself.
        CHECK_NEAR(extractYaw(filter.orientation()), kPi / 2.0, 1e-6);
        CHECK_NEAR(filter.yawOffset(), kPi / 2.0, 1e-9);

        // Turning the head still moves the view by the same amount.
        filter.integrateGyro(Vec3 {0.0, 0.3, 0.0}, 1.0);
        CHECK_NEAR(extractYaw(filter.orientation()), kPi / 2.0 + 0.3, 1e-6);
    }

    // --- and survives a tracker reconnect ---
    {
        OrientationFilter filter;
        filter.setYawOffset(-1.0);
        filter.reset();
        CHECK_NEAR(filter.yawOffset(), -1.0, 1e-9);
        CHECK_NEAR(extractYaw(filter.orientation()), -1.0, 1e-6);
    }

    // --- a nonsense offset is ignored rather than poisoning the estimate ---
    {
        OrientationFilter filter;
        filter.setYawOffset(0.5);
        filter.setYawOffset(std::nan(""));
        CHECK_NEAR(filter.yawOffset(), 0.5, 1e-9);
    }

    // --- degenerate inputs do not produce NaNs ---
    {
        OrientationFilter filter;
        filter.integrateGyro(Vec3 {0.0, 0.0, 0.0}, 0.0);
        filter.integrateGyro(Vec3 {1.0, 1.0, 1.0}, -1.0);
        filter.applyGravity(Vec3 {0.0, 0.0, 0.0}, 0.001);
        const Quat q = filter.orientation();
        CHECK(std::isfinite(q.w) && std::isfinite(q.x)
            && std::isfinite(q.y) && std::isfinite(q.z));
        CHECK_NEAR(q.w, 1.0, 1e-9);
    }
}

} // namespace dk2vr::tests
