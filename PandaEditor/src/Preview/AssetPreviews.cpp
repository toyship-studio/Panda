#include "Preview/AssetPreviews.hpp"

#include "Project/Loader/AssetHandlerEditor.hpp"

#include <Foundation/GraphicsAPI.hpp>
#include <Panda/Assets/Base/AssetImporterBase.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/Renderer/Std140Buffer.hpp>

#include <algorithm>

namespace Panda {

namespace {

    AssetPreviews *s_shared = nullptr;

    constexpr uint32_t MAX_PREVIEW_SIDE = 2048; // px: a preview is a part of a form, not a screen

    AssetType typeOf(AssetId assetId) {
        auto *handler = static_cast<AssetHandlerEditor *>(GameContext::getAssetHandler());
        if (handler == nullptr) { return AssetType::NONE; }
        const auto it = handler->getRegistry().find(assetId);
        return it == handler->getRegistry().end() ? AssetType(AssetType::NONE) : it->second.type;
    }

    Miren::TextureHandle createDisplayTexture(uint32_t width, uint32_t height) {
        Miren::TextureCreate create;
        create.m_minFiltering = Miren::LINEAR;
        create.m_magFiltering = Miren::LINEAR;
        create.m_wrap = Miren::CLAMP;
        create.m_format = Miren::TextureFormat::RGBA8;
        create.m_width = static_cast<uint16_t>(width);
        create.m_height = static_cast<uint16_t>(height);
        return Miren::createTexture(create);
    }

    Miren::FrameBufferHandle createFrameBuffer(Miren::TextureHandle color) {
        Miren::FrameBufferAttachment attachments[] = {color};
        return Miren::createFrameBuffer(Miren::FrameBufferSpecification(attachments, 1));
    }

} // namespace

AssetPreviews::AssetPreviews(WrapTexture wrap, ReleaseTexture release)
    : m_wrap(std::move(wrap))
    , m_release(std::move(release)) {}

AssetPreviews::~AssetPreviews() {
    clear();
    if (m_blitProgram.isValid()) { Miren::deleteProgram(m_blitProgram); }
    for (Miren::FrameBufferHandle frameBuffer : m_retiredFrameBuffers) { Miren::deleteFrameBuffer(frameBuffer); }
    for (Miren::TextureHandle texture : m_retiredTextures) { Miren::deleteTexture(texture); }
    for (PandaUI::TextureHandle handle : m_retiredHandles) { m_release(handle); }
}

AssetPreviews *AssetPreviews::shared() {
    return s_shared;
}

void AssetPreviews::setShared(AssetPreviews *previews) {
    s_shared = previews;
}

bool AssetPreviews::hasPreview(AssetType type) {
    return type == AssetType::MATERIAL || type == AssetType::TEXTURE || type == AssetType::IMPORTED_MESH;
}

void AssetPreviews::update() {
    // What the previous frame took off display is drawn and unbound by now.
    for (Miren::FrameBufferHandle frameBuffer : m_retiredFrameBuffers) { Miren::deleteFrameBuffer(frameBuffer); }
    m_retiredFrameBuffers.clear();
    for (Miren::TextureHandle texture : m_retiredTextures) { Miren::deleteTexture(texture); }
    m_retiredTextures.clear();
    for (PandaUI::TextureHandle handle : m_retiredHandles) { m_release(handle); }
    m_retiredHandles.clear();
    // The thumbnail shot in the last frame is drawn: its assets are not held any longer (a
    // model with its textures does not stay in memory for the sake of a thumbnail).
    if (m_thumbnailStage) { m_thumbnailStage->clear(); }
    // Released leases (the form was rebound or closed).
    for (Preview &preview : m_previews) {
        if (preview.lease.expired()) { releasePreview(preview); }
    }
    std::erase_if(m_previews, [](const Preview &preview) { return preview.lease.expired(); });
    if (!m_renderer && (!m_previews.empty() || !m_queue.empty())) { m_renderer = std::make_unique<PreviewRenderer>(); }
    renderLivePreview();
    if (!renderPendingDisplayCopy()) { renderNextThumbnail(); }
}

std::shared_ptr<AssetPreview> AssetPreviews::lease(AssetId assetId) {
    const AssetType type = typeOf(assetId);
    if (!hasPreview(type)) { return nullptr; }
    Preview preview;
    preview.assetId = assetId;
    auto lease = std::make_shared<AssetPreview>();
    if (type == AssetType::TEXTURE) {
        AssetRef<TextureAsset> texture(assetId);
        if (!texture || !texture->getMirenHandle().isValid()) { return nullptr; }
        lease->kind = AssetPreview::Kind::Image;
        preview.pinned = texture;
        // An sRGB texture cannot be given to the UI as is: a sample is decoded to linear by the
        // hardware, while the UI is display-space, so the image would darken. update() builds
        // the display copy (a blit that encodes).
        const Miren::TextureFormat format = texture->getFormat();
        if (format == Miren::TextureFormat::RGBA8_SRGB || format == Miren::TextureFormat::BC7_SRGB) {
            preview.displayCopyPending = true;
        } else {
            const Size size = texture->getSize();
            lease->texture = m_wrap(texture->getMirenHandle(), PandaUI::Size(size.width, size.height));
        }
    } else {
        lease->kind = AssetPreview::Kind::Stage;
        lease->hasShape = type == AssetType::MATERIAL;
        lease->orbit = PreviewStage::defaultOrbit(type);
        // A render target: GL rows go bottom to top, D3D and Metal rows top to bottom.
        lease->flipVertical = Foundation::ACTIVE_GRAPHICS_TRAITS.originBottomLeft;
        lease->requested = true; // the first frame at once, without waiting for the tick of the form
        preview.stage = std::make_unique<PreviewStage>();
    }
    preview.lease = lease;
    m_previews.push_back(std::move(preview));
    return lease;
}

// The previews of the forms on screen take turns, one a frame (usually one form is visible, and
// it is drawn every frame).
void AssetPreviews::renderLivePreview() {
    const size_t count = m_previews.size();
    for (size_t step = 0; step < count; step++) {
        Preview &preview = m_previews[(m_previewCursor + step) % count];
        const std::shared_ptr<AssetPreview> lease = preview.lease.lock();
        if (!preview.stage || !lease || !lease->requested) { continue; }
        m_previewCursor = (m_previewCursor + step + 1) % count;
        // The stage: set up again when the asset asked for it or the shape is another one. An
        // asset that is not ready leaves the picture as it was; the next frame tries again.
        if (preview.stale || preview.stage->isEmpty() || preview.stage->shape() != lease->shape) {
            if (!preview.stage->show(preview.assetId, lease->shape)) { break; }
            preview.stale = false;
        }
        // The target follows the size the view shows it at.
        const uint32_t width = std::clamp<uint32_t>(lease->width, 16, MAX_PREVIEW_SIDE);
        const uint32_t height = std::clamp<uint32_t>(lease->height, 16, MAX_PREVIEW_SIDE);
        const bool resized = !preview.color.isValid() || preview.width != width || preview.height != height;
        if (resized) {
            if (preview.frameBuffer.isValid()) { m_retiredFrameBuffers.push_back(preview.frameBuffer); }
            // The old texture stays on display until this frame is drawn: the handle of the
            // lease changes below, the form picks the new one up on its next tick.
            retireTexture(preview.color, lease->texture);
            preview.color = createDisplayTexture(width, height);
            preview.frameBuffer = createFrameBuffer(preview.color);
            preview.width = width;
            preview.height = height;
        }
        // The pass runs before the UI of the same frame (the order of the Miren views).
        m_renderer->render(PreviewRenderer::Slot::Live, *preview.stage, lease->orbit, preview.frameBuffer, width, height);
        if (resized) { lease->texture = m_wrap(preview.color, PandaUI::Size(static_cast<float>(width), static_cast<float>(height))); }
        break;
    }
    // The request lives for one frame: a visible form sets it again, a hidden one does not.
    for (Preview &preview : m_previews) {
        if (const std::shared_ptr<AssetPreview> lease = preview.lease.lock()) { lease->requested = false; }
    }
}

bool AssetPreviews::renderPendingDisplayCopy() {
    for (Preview &preview : m_previews) {
        if (!preview.displayCopyPending) { continue; }
        preview.displayCopyPending = false;
        const std::shared_ptr<AssetPreview> lease = preview.lease.lock();
        TextureAsset *texture = preview.pinned.get();
        if (!lease || texture == nullptr) { continue; }
        preview.displayCopy = renderDisplayCopy(*texture);
        const Size size = texture->getSize();
        lease->texture = m_wrap(
            preview.displayCopy.isValid() ? preview.displayCopy : texture->getMirenHandle(),
            PandaUI::Size(size.width, size.height)
        );
        return true;
    }
    return false;
}

void AssetPreviews::retireTexture(Miren::TextureHandle texture, PandaUI::TextureHandle ui) {
    if (texture.isValid()) { m_retiredTextures.push_back(texture); }
    if (ui.isValid()) { m_retiredHandles.push_back(ui); }
}

void AssetPreviews::releasePreview(Preview &preview) {
    // The lease may still be alive (project closing): the form sees an empty texture and hides
    // the preview. The GPU resources outlive the current frame — the old handle may still be in
    // the draw list.
    PandaUI::TextureHandle shown;
    if (const std::shared_ptr<AssetPreview> lease = preview.lease.lock()) {
        shown = lease->texture;
        lease->texture = PandaUI::TextureHandle();
    }
    if (preview.frameBuffer.isValid()) { m_retiredFrameBuffers.push_back(preview.frameBuffer); }
    retireTexture(preview.color, shown); // the handle goes also when it wraps a texture that is not ours (an image)
    retireTexture(preview.displayCopy);
    preview = Preview{};
}

PandaUI::TextureHandle AssetPreviews::thumbnailFor(AssetId assetId) {
    auto it = m_thumbnails.find(assetId);
    if (it != m_thumbnails.end()) { return it->second.ui; }
    enqueue(assetId);
    return PandaUI::TextureHandle();
}

void AssetPreviews::prefetch(AssetId assetId) {
    if (m_thumbnails.contains(assetId)) { return; }
    enqueue(assetId);
}

void AssetPreviews::enqueue(AssetId assetId) {
    if (std::find(m_queue.begin(), m_queue.end(), assetId) != m_queue.end()) { return; }
    m_queue.push_back(assetId);
}

void AssetPreviews::cancelPending() {
    m_queue.clear();
}

void AssetPreviews::invalidate(AssetId assetId) {
    // The thumbnail texture is NOT deleted and NOT evicted from the cache: its handle is held by
    // live grid cells. It is redrawn into the SAME texture — the thumbnail updates "live".
    if (m_thumbnails.contains(assetId)) { enqueue(assetId); }
    for (Preview &preview : m_previews) {
        if (preview.assetId == assetId) { preview.stale = true; }
    }
}

void AssetPreviews::clear() {
    m_queue.clear();
    // The project assets die — previews must hold neither references nor ids.
    for (Preview &preview : m_previews) { releasePreview(preview); }
    m_previews.clear();
    m_thumbnailStage.reset();
    for (auto &entry : m_thumbnails) {
        Miren::deleteTexture(entry.second.native);
        m_release(entry.second.ui);
    }
    m_thumbnails.clear();
}

void AssetPreviews::blit(Miren::TextureHandle source, const glm::vec4 &fit, bool encodeSrgb) {
    if (!m_blitProgram.isValid()) {
        m_blitProgram = Miren::createProgram(
            AssetImporterBase::loadShaderProgram("default-shaders/postprocess.vert", "editor-shaders/preview_blit.frag")
        );
    }
    if (!m_blitQuad) { m_blitQuad = std::make_unique<FullscreenQuad>(); }
    Std140Buffer block;
    block.addVec4(fit.x, fit.y, fit.z, fit.w);
    block.addVec4(encodeSrgb ? 1.f : 0.f, 0.f, 0.f, 0.f);
    Miren::setState(0); // no depth/cull: a flat quad over the whole target
    Miren::addInputTexture("inputTexture", source);
    Miren::addInputUniformBuffer("BLIT_FIT", block.getData(), block.getSize());
    m_blitQuad->submit(PreviewRenderer::displayView(PreviewRenderer::Slot::Thumbnail), m_blitProgram);
}

void AssetPreviews::renderTextureThumbnail(AssetId textureId) {
    // The AssetRef is local: after the pass the refcount drops and the full-size texture may be
    // unloaded — only the square thumbnail stays in the cache.
    AssetRef<TextureAsset> texture(textureId);
    if (!texture || !texture->getMirenHandle().isValid()) { return; }
    // Fit the source frame into a square: the long side in full, the short side gets margins.
    const Size size = texture->getSize();
    const float aspect = (size.height > 0.f) ? (size.width / size.height) : 1.f;
    glm::vec4 fit(1.f, aspect, 0.f, -(aspect - 1.f) * 0.5f);
    if (aspect < 1.f) {
        const float inverse = 1.f / aspect;
        fit = glm::vec4(inverse, 1.f, -(inverse - 1.f) * 0.5f, 0.f);
    }
    const Miren::ViewId view = PreviewRenderer::displayView(PreviewRenderer::Slot::Thumbnail);
    Miren::setViewClear(view, 0x00000000);
    Miren::setViewport(view, Miren::Rect(0, 0, THUMBNAIL_SIZE, THUMBNAIL_SIZE));
    const Miren::TextureFormat format = texture->getFormat();
    blit(texture->getMirenHandle(), fit, format == Miren::TextureFormat::RGBA8_SRGB || format == Miren::TextureFormat::BC7_SRGB);
}

Miren::TextureHandle AssetPreviews::renderDisplayCopy(TextureAsset &texture) {
    Miren::TextureHandle source = texture.getMirenHandle();
    if (!source.isValid()) { return {}; }
    const Size size = texture.getSize();
    Miren::TextureHandle copy = createDisplayTexture(static_cast<uint32_t>(size.width), static_cast<uint32_t>(size.height));
    if (!copy.isValid()) { return {}; }
    Miren::FrameBufferHandle frameBuffer = createFrameBuffer(copy);
    const Miren::ViewId view = PreviewRenderer::displayView(PreviewRenderer::Slot::Thumbnail);
    Miren::setViewFrameBuffer(view, frameBuffer);
    Miren::setViewClear(view, 0x00000000);
    Miren::setViewport(view, Miren::Rect(0, 0, static_cast<int>(size.width), static_cast<int>(size.height)));
    // The frame 1:1, no letterbox; the caller knows the source is sRGB.
    blit(source, glm::vec4(1.f, 1.f, 0.f, 0.f), true);
    m_retiredFrameBuffers.push_back(frameBuffer); // needed only for the duration of the pass
    return copy;
}

void AssetPreviews::renderNextThumbnail() {
    if (m_queue.empty()) { return; }
    const AssetId assetId = m_queue.front();
    m_queue.pop_front();
    // The queue is public (panels call thumbnailFor / prefetch): an asset without a picture is
    // silently dropped.
    const AssetType type = typeOf(assetId);
    if (!hasPreview(type)) { return; }
    const bool isTexture = type == AssetType::TEXTURE;
    if (!isTexture) {
        if (!m_thumbnailStage) { m_thumbnailStage = std::make_unique<PreviewStage>(); }
        // An asset that is not ready (a shader that still compiles) gets no thumbnail yet: the
        // cell repeats the request.
        if (!m_thumbnailStage->show(assetId, PrimitiveMeshKind::Sphere)) { return; }
    }

    // An update of an existing thumbnail is rendered INTO THE SAME texture: its handle may be
    // held by live grid cells, the id must stay valid (and the image updates without a rebind).
    const auto existing = m_thumbnails.find(assetId);
    const bool isNew = existing == m_thumbnails.end();
    const Miren::TextureHandle color = isNew ? createDisplayTexture(THUMBNAIL_SIZE, THUMBNAIL_SIZE) : existing->second.native;
    const Miren::FrameBufferHandle frameBuffer = createFrameBuffer(color);
    if (isTexture) {
        Miren::setViewFrameBuffer(PreviewRenderer::displayView(PreviewRenderer::Slot::Thumbnail), frameBuffer);
        renderTextureThumbnail(assetId);
    } else {
        m_renderer->render(
            PreviewRenderer::Slot::Thumbnail, *m_thumbnailStage, PreviewStage::defaultOrbit(type), frameBuffer,
            THUMBNAIL_SIZE, THUMBNAIL_SIZE
        );
    }
    m_retiredFrameBuffers.push_back(frameBuffer); // the thumbnail does not need it after the pass

    if (isNew) {
        Thumbnail thumbnail;
        thumbnail.native = color;
        thumbnail.ui = m_wrap(color, PandaUI::Size(THUMBNAIL_SIZE, THUMBNAIL_SIZE));
        m_thumbnails[assetId] = thumbnail;
    }
    if (m_onThumbnailReady) { m_onThumbnailReady(assetId); }
}

} // namespace Panda
