#pragma once

#include <Panda/GameLogic/Components/BaseComponents.hpp> // PrimitiveMeshKind

#include <PandaUI/Core/TextureHandle.hpp>

#include <cstdint>

namespace Panda {

// Where the camera of a preview stands: it orbits what the stage shows. The angles are those of
// the viewport camera (yaw around the vertical axis, pitch up and down), so the same gestures
// turn both; the camera looks at the middle of the subject from a distance that fits it into the
// frame, divided by the zoom.
struct PreviewOrbit {
    float yaw = 0.f;   // radians; 0 — from the front (+Z), positive — the camera turns left (it moves to the right of the subject)
    float pitch = 0.f; // radians; negative — the camera looks down (it is above the subject)
    float zoom = 1.f;  // 1 — the subject fits the frame; above — closer

    bool operator==(const PreviewOrbit &other) const = default;
};

// The large preview of an asset form (AssetPreviews::lease): a little viewport on a material or
// a model — a stage drawn through an orbit camera — or the image of a texture. The preview
// belongs to the form through the lease (shared_ptr): while the lease is alive its resources
// are, and the view of the form talks to the renderer through it — it sets what it wants
// (the size, the orbit, the shape, "I am on screen"), the renderer fills in the texture.
struct AssetPreview {
    enum class Kind {
        Stage, // a material on a primitive or a model, seen through the orbit
        Image, // a texture as it is
    };
    Kind kind = Kind::Stage;
    // Invalid until the first frame is rendered, and after the project is closed.
    PandaUI::TextureHandle texture;
    bool flipVertical = false; // a render target: in GL the rows go bottom to top

    // The view → the renderer.
    // The view is on screen: it sets the flag itself every tick while visible. A stage is
    // redrawn only on request — hidden tabs do not spend a frame.
    bool requested = false;
    PreviewOrbit orbit;
    // A material: the primitive it is shown on. hasShape — the preview is of a material (the
    // view offers the choice); a model has a shape of its own.
    bool hasShape = false;
    PrimitiveMeshKind shape = PrimitiveMeshKind::Sphere;
    // The pixels the view shows the stage at: the render target follows (a stage is drawn at
    // the size it is looked at, like a viewport).
    uint32_t width = 256;
    uint32_t height = 256;
};

} // namespace Panda
