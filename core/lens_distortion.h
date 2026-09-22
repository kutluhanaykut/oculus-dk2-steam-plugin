// DK2 lens distortion.
//
// The DK2's lenses magnify with pincushion distortion, so the image sent to
// the panel has to be pre-warped with the opposite barrel distortion. SteamVR
// asks for this through IVRDisplayComponent::ComputeDistortion: given a point
// in the eye's output viewport, it wants the point in the rendered image that
// should appear there.
//
// The radial polynomial is the well known Rift approximation,
//     f(r) = k0 + k1*r^2 + k2*r^4 + k3*r^6
// evaluated in an aspect-corrected, lens-centred space. Dividing by f at the
// fit radius keeps the warped image filling the viewport instead of shrinking
// inside it.
#pragma once

namespace dk2vr {

struct DistortionUv {
    float u {0.0F};
    float v {0.0F};
};

struct DistortionCoefficients {
    // Radial polynomial terms. The defaults are the published DK2 values.
    float k0 {1.0F};
    float k1 {0.22F};
    float k2 {0.24F};
    float k3 {0.0F};

    // Per-channel scaling that compensates for the lenses bending red and
    // blue by different amounts. 0 disables chromatic correction.
    float chromaticAberration {0.006F};

    // Radius at which the warped image should meet the viewport edge. 1.0
    // means the top and bottom edges line up.
    float fitRadius {1.0F};
};

struct EyeLensGeometry {
    // Lens centre inside the eye's viewport, in 0..1 coordinates.
    DistortionUv lensCenter {0.5F, 0.5F};
    // Width over height of the eye's physical area on the panel. Used so the
    // distortion stays radially symmetric on a non-square viewport.
    float aspect {1.0F};
};

enum class ColorChannel {
    Red,
    Green,
    Blue,
};

// Value of the radial polynomial at the fit radius. Precomputed once per eye
// rather than per pixel.
float fitScale(const DistortionCoefficients& coefficients);

// Maps a viewport coordinate to the render-target coordinate to sample.
// `scale` comes from fitScale(). Results outside 0..1 are expected at the
// edges and mean "nothing to show here"; the compositor blacks them out.
DistortionUv applyBarrel(const DistortionCoefficients& coefficients,
    const EyeLensGeometry& geometry,
    DistortionUv output,
    ColorChannel channel,
    float scale);

// Horizontal offset of a lens centre from the middle of its half of the
// panel, in 0..1 viewport units. Positive moves the left eye's lens toward
// the centre of the panel.
float lensCenterOffset(float screenWidthMeters, float lensSeparationMeters);

} // namespace dk2vr
