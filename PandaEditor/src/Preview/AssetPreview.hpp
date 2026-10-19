#pragma once

#include <PandaUI/Core/TextureHandle.hpp>

namespace Panda {

// Large preview of an asset form (editor tab): the material sphere or the image itself. The
// preview belongs to the FORM through a lease (shared_ptr): several tabs with asset forms are open,
// and each has its own texture — a shared slot would show the asset of the last bound form in all
// tabs. While the lease is alive, the preview resources are alive; when the form is rebound/closed,
// the lease is released and the resources are removed a frame later.
struct AssetPreview {
    // Invalid until the first preview frame is rendered (the form picks the texture up on its own
    // tick), and after the project is closed.
    PandaUI::TextureHandle texture;
    bool flipVertical = false;      // FBO: in GL the rows go bottom to top
    bool plate = false;             // image — on a square plate; sphere — on a transparent one
    // The form is on screen: it sets the flag itself every tick while visible. The live preview
    // (material) is redrawn only on request — hidden tabs do not spend a frame.
    bool requested = false;
};

} // namespace Panda
