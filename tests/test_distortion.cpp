#include "test_support.h"

#include "core/lens_distortion.h"

#include <cmath>

namespace dk2vr::tests {

void runDistortionTests()
{
    DistortionCoefficients coefficients; // DK2 defaults
    EyeLensGeometry geometry;
    geometry.aspect = 0.889F;
    geometry.lensCenter = {0.5F, 0.5F};

    const float scale = fitScale(coefficients);

    // --- the polynomial at the fit radius is what normalises the warp ---
    CHECK_NEAR(scale, 1.0 + 0.22 + 0.24, 1e-5);

    // --- the lens centre is a fixed point ---
    {
        const DistortionUv centre = applyBarrel(coefficients, geometry,
            DistortionUv {0.5F, 0.5F}, ColorChannel::Green, scale);
        CHECK_NEAR(centre.u, 0.5, 1e-6);
        CHECK_NEAR(centre.v, 0.5, 1e-6);
    }

    // --- the warp pulls the middle of the image inward ---
    // With a barrel pre-warp the sampled point is closer to the centre than
    // the output point, everywhere except at the fit radius.
    {
        const DistortionUv sampled = applyBarrel(coefficients, geometry,
            DistortionUv {0.5F, 0.25F}, ColorChannel::Green, scale);
        CHECK(std::abs(sampled.v - 0.5F) < std::abs(0.25F - 0.5F));
        CHECK_NEAR(sampled.u, 0.5, 1e-6);
    }

    // --- at the fit radius the image meets the viewport edge ---
    // v = 0 is one vertical half-extent from the centre, which is radius 1.
    {
        const DistortionUv edge = applyBarrel(coefficients, geometry,
            DistortionUv {0.5F, 0.0F}, ColorChannel::Green, scale);
        CHECK_NEAR(edge.v, 0.0, 1e-5);
    }

    // --- the warp is radially symmetric about the lens centre ---
    {
        const DistortionUv above = applyBarrel(coefficients, geometry,
            DistortionUv {0.5F, 0.3F}, ColorChannel::Green, scale);
        const DistortionUv below = applyBarrel(coefficients, geometry,
            DistortionUv {0.5F, 0.7F}, ColorChannel::Green, scale);
        CHECK_NEAR(0.5 - above.v, below.v - 0.5, 1e-6);
    }

    // --- red and blue separate, green stays put ---
    {
        const DistortionUv point {0.5F, 0.2F};
        const DistortionUv red = applyBarrel(coefficients, geometry, point,
            ColorChannel::Red, scale);
        const DistortionUv green = applyBarrel(coefficients, geometry, point,
            ColorChannel::Green, scale);
        const DistortionUv blue = applyBarrel(coefficients, geometry, point,
            ColorChannel::Blue, scale);

        // Red is bent further out than green, blue less far.
        CHECK(std::abs(red.v - 0.5F) > std::abs(green.v - 0.5F));
        CHECK(std::abs(blue.v - 0.5F) < std::abs(green.v - 0.5F));
    }

    // --- chromatic correction off means all three channels agree ---
    {
        DistortionCoefficients mono = coefficients;
        mono.chromaticAberration = 0.0F;
        const float monoScale = fitScale(mono);
        const DistortionUv point {0.5F, 0.2F};
        const DistortionUv red = applyBarrel(mono, geometry, point, ColorChannel::Red,
            monoScale);
        const DistortionUv blue = applyBarrel(mono, geometry, point, ColorChannel::Blue,
            monoScale);
        CHECK_NEAR(red.u, blue.u, 1e-9);
        CHECK_NEAR(red.v, blue.v, 1e-9);
    }

    // --- the aspect correction keeps the warp circular on a non-square eye ---
    // Points the same physical distance from the lens, one horizontal and one
    // vertical, must be displaced by the same physical amount.
    {
        const float horizontalOffset = 0.25F / geometry.aspect;
        const DistortionUv horizontal = applyBarrel(coefficients, geometry,
            DistortionUv {0.5F + horizontalOffset, 0.5F}, ColorChannel::Green, scale);
        const DistortionUv vertical = applyBarrel(coefficients, geometry,
            DistortionUv {0.5F, 0.5F + 0.25F}, ColorChannel::Green, scale);

        const double horizontalPhysical = (horizontal.u - 0.5) * geometry.aspect;
        const double verticalPhysical = vertical.v - 0.5;
        CHECK_NEAR(horizontalPhysical, verticalPhysical, 1e-6);
    }

    // --- the lens centre offset follows the DK2's geometry ---
    {
        // Lens separation is very close to half the panel width, so the
        // lenses sit almost exactly in the middle of each half.
        const float offset = lensCenterOffset(0.12576F, 0.0635F);
        CHECK(std::abs(offset) < 0.02F);

        // A wider separation pushes each lens outward from its half's centre.
        CHECK(lensCenterOffset(0.12576F, 0.08F) > offset);

        // Nonsense input does not produce a NaN.
        CHECK_NEAR(lensCenterOffset(0.0F, 0.0635F), 0.0, 1e-9);
    }

    // --- a collapsed coefficient set does not divide the image away ---
    {
        DistortionCoefficients broken;
        broken.k0 = 0.0F;
        broken.k1 = 0.0F;
        broken.k2 = 0.0F;
        broken.k3 = 0.0F;
        CHECK_NEAR(fitScale(broken), 1.0, 1e-9);
    }
}

} // namespace dk2vr::tests
