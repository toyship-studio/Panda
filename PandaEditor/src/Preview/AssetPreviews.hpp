#pragma once

#include "Preview/AssetPreview.hpp"
#include "Preview/PreviewRenderer.hpp"
#include "Preview/PreviewStage.hpp"

#include <Panda/Assets/TextureAsset.hpp>
#include <Panda/Renderer/FullscreenQuad.hpp>

#include <PandaUI/Core/TextureHandle.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Panda {

// The pictures of assets in the editor (context/architecture.md, "Previews"): the large preview
// of an asset form — a little viewport on a material or a model, or the image of a texture —
// and the thumbnails of the Content Browser and of the asset fields. A material and a model are
// shown the same way, by a PreviewStage (a little world) drawn by the PreviewRenderer; a
// thumbnail is one frame of a stage, a preview is a stage of its own that the form looks at
// through its orbit. A texture is its own picture: a copy for the display.
//
// Everything is drawn from update(), once a frame: one preview of a form (the visible ones take
// turns) and one thumbnail from the queue — a frame has one pair of views for each.
class AssetPreviews final {
public:
    static constexpr uint32_t THUMBNAIL_SIZE = 128; // px, the side of a thumbnail

    // The UI host's side of a texture: a native Miren texture under a PandaUI handle, and the
    // handle given back when the texture goes.
    using WrapTexture = std::function<PandaUI::TextureHandle(Miren::TextureHandle, PandaUI::Size)>;
    using ReleaseTexture = std::function<void(PandaUI::TextureHandle)>;

    AssetPreviews(WrapTexture wrap, ReleaseTexture release);
    ~AssetPreviews();

    // Global access point for panels (in the spirit of GameContext); set/cleared by EditorLayer.
    static AssetPreviews *shared();
    static void setShared(AssetPreviews *previews);

    // The kinds of assets that have a picture of their own: a thumbnail and a large preview.
    static bool hasPreview(AssetType type);

    void update(); // once per frame

    // A thumbnail was built or redrawn: grid cells are configured once, so without a
    // notification a visible cell would stay with the type icon.
    void setOnThumbnailReady(std::function<void(AssetId)> callback) {
        m_onThumbnailReady = std::move(callback);
    }

    // The large preview of an asset for a form (AssetPreview). nullptr — the asset has none.
    // Every lease is a preview of its own: two forms of one asset turn their own cameras.
    std::shared_ptr<AssetPreview> lease(AssetId assetId);

    // Thumbnails: immediately from the cache; otherwise queued, and meanwhile an invalid handle.
    PandaUI::TextureHandle thumbnailFor(AssetId assetId);
    void prefetch(AssetId assetId); // schedule the build ahead of time (grid prefetch)
    void cancelPending();           // folder change: the queue is no longer relevant (the cache stays)
    // The asset changed in a way a picture of it does not follow by itself (a reimport, other
    // import settings): its thumbnail is redrawn into the same texture, its previews are set up again.
    void invalidate(AssetId assetId);
    void clear(); // project closing: cache, queue and previews are reset

private:
    struct Thumbnail {
        Miren::TextureHandle native;
        PandaUI::TextureHandle ui;
    };

    // The resources of one large preview; they live while its lease does (weak — the form owns it).
    struct Preview {
        AssetId assetId = 0;
        std::weak_ptr<AssetPreview> lease;
        // A stage: the little world, and the display texture the frame of the stage is finalized into.
        std::unique_ptr<PreviewStage> stage;
        bool stale = true; // the stage is set up again before the next frame
        Miren::TextureHandle color;
        Miren::FrameBufferHandle frameBuffer;
        uint32_t width = 0;
        uint32_t height = 0;
        // An image: the ref keeps the texture alive; the display copy is for sRGB (empty — the
        // texture itself is shown).
        AssetRef<TextureAsset> pinned;
        Miren::TextureHandle displayCopy;
        bool displayCopyPending = false;
    };

    void releasePreview(Preview &preview); // the resources go to the retire lists, the lease gets an empty texture
    void retireTexture(Miren::TextureHandle texture, PandaUI::TextureHandle ui = {});
    void renderLivePreview();
    bool renderPendingDisplayCopy(); // true — the display view of the thumbnail slot is busy this frame
    void renderNextThumbnail();
    // A thumbnail of a TEXTURE: a copy into a square (letterbox) by a blit pass — the source is
    // already display-referred, there is nothing to light or tonemap. An sRGB source is sampled
    // as linear: the blit encodes it back.
    void renderTextureThumbnail(AssetId textureId); // the caller sets up the target
    // A display copy of a full-resolution sRGB texture for the large preview (PandaUI works in
    // display space and does not encode sRGB itself). One blit per lease.
    Miren::TextureHandle renderDisplayCopy(TextureAsset &texture);
    void blit(Miren::TextureHandle source, const glm::vec4 &fit, bool encodeSrgb);
    void enqueue(AssetId assetId);

    WrapTexture m_wrap;
    ReleaseTexture m_release;
    std::function<void(AssetId)> m_onThumbnailReady;
    // Made with the first picture: Miren is up by then.
    std::unique_ptr<PreviewRenderer> m_renderer;
    std::unique_ptr<FullscreenQuad> m_blitQuad;
    Miren::ProgramHandle m_blitProgram;

    std::vector<Preview> m_previews;
    size_t m_previewCursor = 0; // the visible previews take turns, one a frame

    std::unordered_map<AssetId, Thumbnail> m_thumbnails;
    std::deque<AssetId> m_queue; // thumbnails to build (one per frame)
    // The stage the thumbnails are shot on, one after another. It holds the asset of the last
    // shot until the next update: the frame that draws it is still on its way to the GPU.
    std::unique_ptr<PreviewStage> m_thumbnailStage;

    // What was taken off display: deleted in update() a frame later, when nothing draws it or
    // into it any more.
    std::vector<Miren::FrameBufferHandle> m_retiredFrameBuffers;
    std::vector<Miren::TextureHandle> m_retiredTextures;
    std::vector<PandaUI::TextureHandle> m_retiredHandles;
};

} // namespace Panda
