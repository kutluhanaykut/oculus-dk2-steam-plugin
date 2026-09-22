#include "core/lens_distortion.h"

#include <algorithm>
#include <cmath>

namespace dk2vr {
namespace {

float polynomial(const DistortionCoefficients& coefficients, const float radiusSquared)
{
    const float r2 = radiusSquared;
    const float r4 = r2 * r2;
    const float r6 = r4 * r2;
    return coefficients.k0 + coefficients.k1 * r2 + coefficients.k2 * r4
        + coefficients.k3 * r6;
}

float channelFactor(const DistortionCoefficients& coefficients,
    const ColorChannel channel, const float radiusSquared)
{
    switch (channel) {
    case ColorChannel::Red:
        return 1.0F + coefficients.chromaticAberration * radiusSquared;
    case ColorChannel::Blue:
        return 1.0F - coefficients.chromaticAberration * radiusSquared;
    case ColorChannel::Green:
    default:
        return 1.0F;
    }
}

} // namespace

float fitScale(const DistortionCoefficients& coefficients)
{
    const float radius = coefficients.fitRadius > 0.0F ? coefficients.fitRadius : 1.0F;
    const float value = polynomial(coefficients, radius * radius);
    // Guard against a coefficient set that collapses the image.
    return std::abs(value) > 0.05F ? value : 1.0F;
}

DistortionUv applyBarrel(const DistortionCoefficients& coefficients,
    const EyeLensGeometry& geometry,
    const DistortionUv output,
    const ColorChannel channel,
    const float scale)
{
    const float aspect = geometry.aspect > 0.01F ? geometry.aspect : 1.0F;

    // Lens-centred coordinates, stretched so a circle on the panel stays a
    // circle here. The vertical half-extent is 1.0 by construction.
    const float px = (output.u - geometry.lensCenter.u) * 2.0F * aspect;
    const float py = (output.v - geometry.lensCenter.v) * 2.0F;

    const float radiusSquared = px * px + py * py;
    const float factor = polynomial(coefficients, radiusSquared)
        * channelFactor(coefficients, channel, radiusSquared) / scale;

    const float warpedX = px * factor;
    const float warpedY = py * factor;

    return DistortionUv {
        geometry.lensCenter.u + warpedX / (2.0F * aspect),
        geometry.lensCenter.v + warpedY / 2.0F};
}

float lensCenterOffset(const float screenWidthMeters, const float lensSeparationMeters)
{
    if (!(screenWidthMeters > 0.001F)) {
        return 0.0F;
    }
    // Distance from the centre of one half of the panel to that half's lens,
    // expressed as a fraction of the half's width.
    const float halfWidth = screenWidthMeters * 0.5F;
    const float offsetMeters = lensSeparationMeters * 0.5F - screenWidthMeters * 0.25F;
    return std::clamp(offsetMeters / halfWidth, -0.25F, 0.25F);
}

} // namespace dk2vr
