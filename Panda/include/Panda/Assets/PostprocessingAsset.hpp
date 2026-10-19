#pragma once

#include "Panda/Assets/MaterialAsset.hpp"
#include "Base/AssetRef.hpp"

#include <cstdint>
#include <vector>

namespace Panda {

// One chain pass: a regular material (pass shader + parameter fields) + whether it is enabled.
// downsample — divisor of the pass target resolution (1/2/4/8): a blur at reduced resolution
// is visually wider and cheaper; the last pass of the chain is usually left at full (Full).
struct PostprocessingPass {
    AssetRef<Asset> material;
    bool enabled = true;
    uint32_t downsample = 1;
};

// A chain of fullscreen passes over the scene frame; attached to a camera (CameraComponent).
// Executed by its PostprocessingPipeline.
class PostprocessingAsset : public Asset {
public:
    PostprocessingAsset()
        : Asset(AssetType::POSTPROCESSING) {}
    PostprocessingAsset(std::vector<PostprocessingPass> passes)
        : Asset(AssetType::POSTPROCESSING)
        , m_passes(std::move(passes)) {}

    std::vector<PostprocessingPass> &getPasses() {
        return m_passes;
    }
    // The chain itself converts the frame to display space (carries its own tonemap pass, e.g.
    // ACES) — the engine's built-in fallback Tonemap is then NOT added, and the passes after
    // the tonemap pass work in display space (glare, lens flare, afterimage).
    bool handlesTonemap() const {
        return m_handlesTonemap;
    }
    void setHandlesTonemap(bool value) {
        m_handlesTonemap = value;
    }

#ifdef PND_EDITOR
    void setChanged(bool value) override {
        m_isChanged = value;
    }
    bool isChanged() const override {
        return m_isChanged;
    }
#endif

private:
#ifdef PND_EDITOR
    bool m_isChanged = false;
#endif
    std::vector<PostprocessingPass> m_passes;
    bool m_handlesTonemap = false;
};

} // namespace Panda
