#include "test_support.h"

#include "core/display_geometry.h"

namespace dk2vr::tests {

void runDisplayGeometryTests()
{
    // --- a landscape panel gets side-by-side eyes and no rotation ---
    {
        LayoutRequest request;
        request.x = 1920;
        request.y = 0;
        request.width = 1920;
        request.height = 1080;
        const DisplayLayout layout = computeLayout(request);

        CHECK(layout.split == EyeSplit::Horizontal);
        CHECK(layout.rotation == ImageRotation::None);
        CHECK(layout.windowX == 1920);
        CHECK(layout.renderWidth == 960);
        CHECK(layout.renderHeight == 1080);
    }

    // --- a portrait panel gets stacked eyes and a quarter turn, and the
    //     render target's axes swap so the eye is still drawn upright ---
    {
        LayoutRequest request;
        request.width = 1080;
        request.height = 1920;
        const DisplayLayout layout = computeLayout(request);

        CHECK(layout.split == EyeSplit::Vertical);
        CHECK(layout.rotation == ImageRotation::Cw90);
        // Viewport is 1080x960; rotated, the eye renders 960x1080.
        CHECK(layout.renderWidth == 960);
        CHECK(layout.renderHeight == 1080);
    }

    // --- Windows' own rotation must NOT be compensated for ---
    // Measured on a DK2 that Windows turns by 90 degrees: the image is
    // upright with no rotation applied, so SteamVR inherits the desktop
    // rotation rather than bypassing it. Repeating the turn here is what put
    // the view back on its side.
    {
        LayoutRequest request;
        request.x = 1920;
        request.width = 1920;
        request.height = 1080;
        request.windowsRotationDegrees = 90;
        const DisplayLayout layout = computeLayout(request);

        CHECK(layout.rotation == ImageRotation::None);
        CHECK(layout.split == EyeSplit::Horizontal);
        CHECK(layout.renderWidth == 960);
        CHECK(layout.renderHeight == 1080);
    }
    {
        LayoutRequest request;
        request.width = 1920;
        request.height = 1080;
        request.windowsRotationDegrees = 270;
        CHECK(computeLayout(request).rotation == ImageRotation::None);
    }

    // --- an explicit rotation overrides the guess, which is the escape hatch
    //     when the automatic choice comes out sideways ---
    {
        LayoutRequest request;
        request.width = 1920;
        request.height = 1080;
        request.rotationDegrees = 90;
        const DisplayLayout layout = computeLayout(request);

        CHECK(layout.rotation == ImageRotation::Cw90);
        CHECK(layout.split == EyeSplit::Horizontal); // still auto
        // Viewport 960x1080 turned a quarter means a 1080x960 render target.
        CHECK(layout.renderWidth == 1080);
        CHECK(layout.renderHeight == 960);
    }

    // --- an explicit split overrides the guess too ---
    {
        LayoutRequest request;
        request.width = 1920;
        request.height = 1080;
        request.splitMode = 1; // over-under
        const DisplayLayout layout = computeLayout(request);
        CHECK(layout.split == EyeSplit::Vertical);
        CHECK(layout.renderWidth == 1920);
        CHECK(layout.renderHeight == 540);
    }

    // --- degrees are normalised, and nonsense falls back to no rotation ---
    {
        CHECK(rotationFromDegrees(-90) == ImageRotation::Cw270);
        CHECK(rotationFromDegrees(450) == ImageRotation::Cw90);
        CHECK(rotationFromDegrees(360) == ImageRotation::None);
        CHECK(rotationFromDegrees(0) == ImageRotation::None);
        CHECK(rotationFromDegrees(45) == ImageRotation::None);
    }

    // --- unrotate maps viewport corners onto the right render corners ---
    {
        // No rotation: identity.
        const Uv same = unrotate(ImageRotation::None, Uv {0.25F, 0.75F});
        CHECK_NEAR(same.u, 0.25, 1e-6);
        CHECK_NEAR(same.v, 0.75, 1e-6);

        // 90 clockwise: the output's top-right shows the render's top-left.
        const Uv topRight = unrotate(ImageRotation::Cw90, Uv {1.0F, 0.0F});
        CHECK_NEAR(topRight.u, 0.0, 1e-6);
        CHECK_NEAR(topRight.v, 0.0, 1e-6);

        const Uv topLeft = unrotate(ImageRotation::Cw90, Uv {0.0F, 0.0F});
        CHECK_NEAR(topLeft.u, 0.0, 1e-6);
        CHECK_NEAR(topLeft.v, 1.0, 1e-6);

        // 180: opposite corner.
        const Uv flipped = unrotate(ImageRotation::Cw180, Uv {0.2F, 0.3F});
        CHECK_NEAR(flipped.u, 0.8, 1e-6);
        CHECK_NEAR(flipped.v, 0.7, 1e-6);

        // 270 undoes 90.
        const Uv there = unrotate(ImageRotation::Cw90, Uv {0.3F, 0.6F});
        const Uv back = unrotate(ImageRotation::Cw270, there);
        CHECK_NEAR(back.u, 0.3, 1e-6);
        CHECK_NEAR(back.v, 0.6, 1e-6);

        // The centre is a fixed point for every rotation.
        for (const ImageRotation rotation : {ImageRotation::None, ImageRotation::Cw90,
                 ImageRotation::Cw180, ImageRotation::Cw270}) {
            const Uv centre = unrotate(rotation, Uv {0.5F, 0.5F});
            CHECK_NEAR(centre.u, 0.5, 1e-6);
            CHECK_NEAR(centre.v, 0.5, 1e-6);
        }
    }

    // --- eye viewports tile the panel without overlapping ---
    {
        LayoutRequest request;
        request.width = 1920;
        request.height = 1080;
        const DisplayLayout layout = computeLayout(request);

        const EyeViewport left = eyeViewport(layout, true);
        const EyeViewport right = eyeViewport(layout, false);
        CHECK(left.x == 0 && left.y == 0 && left.width == 960 && left.height == 1080);
        CHECK(right.x == 960 && right.y == 0 && right.width == 960 && right.height == 1080);
    }
    {
        LayoutRequest request;
        request.width = 1080;
        request.height = 1920;
        const DisplayLayout layout = computeLayout(request);

        const EyeViewport left = eyeViewport(layout, true);
        const EyeViewport right = eyeViewport(layout, false);
        CHECK(left.x == 0 && left.y == 0 && left.width == 1080 && left.height == 960);
        CHECK(right.x == 0 && right.y == 960 && right.width == 1080 && right.height == 960);
    }

    // --- swapping the eyes exchanges the two halves ---
    {
        LayoutRequest request;
        request.width = 1920;
        request.height = 1080;
        request.swapEyes = true;
        const DisplayLayout layout = computeLayout(request);

        CHECK(eyeViewport(layout, true).x == 960);
        CHECK(eyeViewport(layout, false).x == 0);
    }

    // --- a zero-sized mode does not produce a degenerate layout ---
    {
        LayoutRequest request;
        request.width = 0;
        request.height = 0;
        const DisplayLayout layout = computeLayout(request);
        CHECK(layout.windowWidth >= 2);
        CHECK(layout.windowHeight >= 2);
        CHECK(layout.renderWidth >= 1);
        CHECK(layout.renderHeight >= 1);
    }
}

} // namespace dk2vr::tests
