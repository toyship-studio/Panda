#pragma once

#include "Preview/AssetPreview.hpp"
#include "Preview/MaterialPreviewRenderer.hpp"

#include <Panda/Assets/TextureAsset.hpp>

#include <PandaUI/Core/TextureHandle.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Panda {

// The sole owner of the editor's preview renders: large previews of asset forms + the thumbnail
// cache. Thumbnails are built asynchronously — one per frame from the queue (update() is called
// by EditorLayer), requests are queued by panels through shared().
class ThumbnailsManager final {
public:
    static constexpr uint32_t THUMBNAIL_SIZE = 128; // px, side of the thumbnail

    // wrap: native Miren texture → PandaUI handle (from the UI host, injected by EditorLayer).
    using WrapTexture = std::function<PandaUI::TextureHandle(Miren::TextureHandle, PandaUI::Size)>;

    explicit ThumbnailsManager(WrapTexture wrap);
    ~ThumbnailsManager();

    // Global access point for panels (in the spirit of GameContext); set/cleared by EditorLayer.
    static ThumbnailsManager *shared();
    static void setShared(ThumbnailsManager *manager);

    void update(); // once per frame: one live preview + one thumbnail from the queue

    // The thumbnail is built/updated: grid cells are configured once, so without a notification a
    // visible cell would stay with the type icon (set by EditorLayer, listened to by ContentBrowser).
    void setOnThumbnailReady(std::function<void(AssetId)> callback) {
        m_onThumbnailReady = std::move(callback);
    }

    // Lease of a large asset preview. Material — a live SIZE×SIZE sphere in its OWN texture (field
    // edits and textures that finished loading are visible immediately). Texture — the image itself
    // at full resolution (not the 128 thumbnail): the asset is pinned while the lease is alive; a
    // display copy is prepared for sRGB. Forms of the same asset share one lease. nullptr — the
    // asset has no preview.
    // GPU preview passes run only from update(): the Miren view state (FBO) is one per frame, so
    // there is one pass per pair of views per frame (live previews — round-robin among the
    // requested ones; the display copy and the thumbnail share THUMBNAIL_TONEMAP_VIEW, so only one
    // of them runs per frame).
    std::shared_ptr<AssetPreview> leasePreview(AssetId assetId);

    // Thumbnails: immediately from the cache; otherwise queued, and meanwhile an invalid handle.
    PandaUI::TextureHandle thumbnailFor(AssetId materialId);
    void prefetch(AssetId materialId);    // schedule the build ahead of time (grid prefetch)
    void cancelPending();                 // folder change: the queue is no longer relevant (the cache stays)
    void invalidate(AssetId materialId);  // the material changed → re-render on the next request
    void clear();                         // project closing: cache and queue are reset

private:
    struct Thumbnail {
        Miren::TextureHandle native;
        PandaUI::TextureHandle ui;
    };

    // Resources of one large preview; they live while the lease is alive (weak — the form owns it).
    struct PreviewEntry {
        AssetId assetId = 0;
        std::weak_ptr<AssetPreview> lease;
        // Material: its own display texture + FBO, into which the sphere frame is finalized.
        bool live = false;
        Miren::TextureHandle color;
        Miren::FrameBufferHandle frameBuffer;
        // Image: the ref keeps the texture alive; the display copy is for sRGB (empty — the texture
        // itself is shown).
        AssetRef<TextureAsset> pinned;
        Miren::TextureHandle displayCopy;
        bool displayCopyPending = false;
    };

    void releasePreview(PreviewEntry &entry); // resources go to the retire lists, the lease gets an empty texture
    void renderLivePreview();
    bool renderPendingDisplayCopy(); // true — THUMBNAIL_TONEMAP_VIEW is busy for this frame
    void ensureRenderer();
    void ensureHdrScratch();
    void ensureBlitQuad();
    Miren::ProgramHandle blitProgram();
    void renderNextThumbnail();
    // Thumbnail of a TEXTURE: a copy into a square (letterbox) by a blit pass. Material rendering
    // goes through the sphere + tonemap; here there is neither: the source is already
    // display-referred. An SRGB source is sampled as linear — the blit does the inverse encode (a
    // flag in BLIT_FIT). The AssetRef lives only for the duration of the pass: the full-size
    // texture does not stay in memory.
    void renderTextureThumbnail(AssetId textureId); // the caller sets up the FBO/view
    // Display copy of a full-resolution SRGB texture for the large preview (PandaUI works
    // in display space and does not re-encode sRGB itself). One blit per lease.
    Miren::TextureHandle renderDisplayCopy(TextureAsset &texture);
    void retireFrameBuffer(Miren::FrameBufferHandle frameBuffer);
    void enqueue(AssetId materialId);

    WrapTexture m_wrap;
    std::function<void(AssetId)> m_onThumbnailReady;
    std::unique_ptr<MaterialPreviewRenderer> m_renderer; // lazily: Miren is ready by the first request
    std::vector<PreviewEntry> m_previews;
    size_t m_previewCursor = 0; // round-robin queue of live previews (one per frame)
    // Preview textures taken off display: deleted in update() a frame later (the UI is already
    // rebound).
    std::vector<Miren::TextureHandle> m_retiredTextures;

    std::unordered_map<AssetId, Thumbnail> m_thumbnails;
    std::deque<AssetId> m_queue; // thumbnails to build (one per frame)
    // Shared HDR scratch target of the thumbnails (16F): the sphere is rendered here, Tonemap goes
    // into the display texture.
    Miren::FrameBufferHandle m_hdrFrameBuffer;
    Miren::FrameBufferSpecification m_hdrFbSpecification;
    Miren::TextureHandle m_hdrColor;
    Miren::TextureHandle m_hdrDepth;
    bool m_hasHdrScratch = false;
    // FBOs of one-off passes (thumbnails, display copies): deleted a frame later, once the render
    // has definitely finished. Several per frame — hence a vector rather than a single slot.
    std::vector<Miren::FrameBufferHandle> m_retiredFrameBuffers;
    // Fullscreen quad for blitting texture thumbnails (created lazily, like the renderer).
    Miren::VertexBufferHandle m_blitVertexBuffer;
    Miren::IndexBufferHandle m_blitIndexBuffer;
    Miren::ProgramHandle m_blitProgram;
    bool m_hasBlitQuad = false;
};

} // namespace Panda
