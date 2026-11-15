#pragma once

#include "Preview/AssetPreview.hpp"

#include <Panda/Assets/Base/Asset.hpp>
#include <PandaUI/PandaUI.hpp>

#include <memory>

namespace Editor {

// VIEW: the large preview of an asset form — a little viewport. A material or a model is a
// stage seen through an orbit camera (AssetPreviews, PreviewStage), turned the way the camera
// of the Scene is: a two-finger swipe of the trackpad or a drag with a mouse button orbits, the
// wheel or a pinch zooms, a double click returns to the first view. A swipe that begins over
// the preview is the preview's, not the form's. A material can be put on another primitive
// (the menu in the corner). A texture is shown as the image it is, and takes no gestures.
class AssetPreviewView final : public PandaUI::Panel {
public:
    AssetPreviewView();

    // The asset the preview is of; 0, or an asset without a picture — the view folds away.
    void setAsset(Panda::AssetId asset);
    // What a test and an agent ask of the camera instead of gestures (EditorMcp).
    Panda::AssetPreview *preview() const {
        return m_lease.get();
    }

    void update(double deltaTime) override; // while on screen: the frame is asked for, at the size shown

protected:
    bool pointerDown(PandaUI::PointerEvent &event) override; // a drag orbits; a double click resets
    bool pointerMove(PandaUI::PointerEvent &event) override;
    bool pointerUp(PandaUI::PointerEvent &event) override;
    bool scroll(PandaUI::ScrollEvent &event) override; // trackpad — orbit, wheel and pinch — zoom
    void interactionCancelled() override;

private:
    bool isStage() const;
    void orbitBy(float dx, float dy); // points of a drag, or of a swipe
    void zoomBy(float factor);
    void applyTexture();
    void rebuildShapeMenu();

    Panda::AssetId m_asset = 0;
    std::shared_ptr<Panda::AssetPreview> m_lease;
    PandaUI::TextureHandle m_shown; // the texture of the lease the image shows now
    std::shared_ptr<PandaUI::ImageView> m_image;
    std::shared_ptr<PandaUI::MenuButton> m_shape; // a material: the primitive it is put on
    bool m_dragging = false;
    PandaUI::PointerButton m_dragButton = PandaUI::PointerButton::None;
};

} // namespace Editor
