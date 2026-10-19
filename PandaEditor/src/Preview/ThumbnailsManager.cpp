#include "Preview/ThumbnailsManager.hpp"

#include "Project/Loader/AssetHandlerEditor.hpp"

#include <Foundation/GraphicsAPI.hpp>
#include <Panda/Assets/Base/AssetImporterBase.hpp>
#include <Panda/Assets/TextureAsset.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/Renderer/MirenViewDistribution.hpp>
#include <Panda/Renderer/Std140Buffer.hpp>

#include <algorithm>

namespace Panda {

namespace {
    ThumbnailsManager *s_shared = nullptr;
}

ThumbnailsManager::ThumbnailsManager(WrapTexture wrap)
    : m_wrap(std::move(wrap)) {}

ThumbnailsManager::~ThumbnailsManager() {
    clear();
    if (m_blitProgram.isValid()) { Miren::deleteProgram(m_blitProgram); }
    for (Miren::FrameBufferHandle frameBuffer : m_retiredFrameBuffers) { Miren::deleteFrameBuffer(frameBuffer); }
    for (Miren::TextureHandle texture : m_retiredTextures) { Miren::deleteTexture(texture); }
    if (m_hasHdrScratch) {
        Miren::deleteFrameBuffer(m_hdrFrameBuffer);
        Miren::deleteTexture(m_hdrColor);
        Miren::deleteTexture(m_hdrDepth);
    }
    if (m_hasBlitQuad) {
        Miren::deleteVertexBuffer(m_blitVertexBuffer);
        Miren::deleteIndexBuffer(m_blitIndexBuffer);
    }
}

ThumbnailsManager *ThumbnailsManager::shared() {
    return s_shared;
}

void ThumbnailsManager::setShared(ThumbnailsManager *manager) {
    s_shared = manager;
}

void ThumbnailsManager::ensureRenderer() {
    if (m_renderer) { return; }
    m_renderer = std::make_unique<MaterialPreviewRenderer>();
}

void ThumbnailsManager::update() {
    // FBOs/textures of the previous frame are already rendered and taken off display — delete them
    // (the color textures of thumbnails live in the cache, preview textures — in m_previews).
    for (Miren::FrameBufferHandle frameBuffer : m_retiredFrameBuffers) { Miren::deleteFrameBuffer(frameBuffer); }
    m_retiredFrameBuffers.clear();
    for (Miren::TextureHandle texture : m_retiredTextures) { Miren::deleteTexture(texture); }
    m_retiredTextures.clear();
    // Released leases (the form was rebound or closed) — release the preview resources.
    for (PreviewEntry &entry : m_previews) {
        if (entry.lease.expired()) { releasePreview(entry); }
    }
    std::erase_if(m_previews, [](const PreviewEntry &entry) { return entry.lease.expired(); });
    renderLivePreview();
    if (!renderPendingDisplayCopy()) { renderNextThumbnail(); }
}

// Live material previews: there is a single set of preview render views, so ONE preview per frame —
// the next requested one in round-robin order from the cursor (usually one form is visible — it is
// the one drawn every frame).
void ThumbnailsManager::renderLivePreview() {
    const size_t count = m_previews.size();
    for (size_t step = 0; step < count; step++) {
        PreviewEntry &entry = m_previews[(m_previewCursor + step) % count];
        const std::shared_ptr<AssetPreview> lease = entry.lease.lock();
        if (!entry.live || !lease || !lease->requested) { continue; }
        ensureRenderer();
        // The texture is handed to the form with the first rendered frame: before that it holds
        // garbage. The preview pass runs before the UI of the same frame (Miren view order).
        if (m_renderer->render(AssetRef<MaterialAsset>(entry.assetId), entry.frameBuffer) &&
            !lease->texture.isValid()) {
            lease->texture = m_wrap(
                entry.color, PandaUI::Size(MaterialPreviewRenderer::SIZE, MaterialPreviewRenderer::SIZE)
            );
        }
        m_previewCursor = (m_previewCursor + step + 1) % count;
        break;
    }
    // The request lives for one frame: a visible form sets it again, a hidden one does not.
    for (PreviewEntry &entry : m_previews) {
        if (const std::shared_ptr<AssetPreview> lease = entry.lease.lock()) { lease->requested = false; }
    }
}

std::shared_ptr<AssetPreview> ThumbnailsManager::leasePreview(AssetId assetId) {
    if (assetId == AssetId(0)) { return nullptr; }
    for (const PreviewEntry &entry : m_previews) {
        if (entry.assetId != assetId) { continue; }
        if (std::shared_ptr<AssetPreview> lease = entry.lease.lock()) { return lease; }
    }
    auto *handler = static_cast<AssetHandlerEditor *>(GameContext::getAssetHandler());
    if (!handler) { return nullptr; }
    // A type guard, not just an existence check: AssetRef<T> on a foreign id resolves an object of
    // ANOTHER class (an unchecked cast).
    const auto it = handler->getRegistry().find(assetId);
    if (it == handler->getRegistry().end()) { return nullptr; }

    PreviewEntry entry;
    entry.assetId = assetId;
    auto lease = std::make_shared<AssetPreview>();
    if (it->second.type == AssetType::MATERIAL) {
        Miren::TextureCreate create;
        create.m_minFiltering = Miren::LINEAR;
        create.m_magFiltering = Miren::LINEAR;
        create.m_wrap = Miren::CLAMP;
        create.m_format = Miren::TextureFormat::RGBA8;
        create.m_width = MaterialPreviewRenderer::SIZE;
        create.m_height = MaterialPreviewRenderer::SIZE;
        entry.color = Miren::createTexture(create);
        Miren::FrameBufferAttachment attachments[] = {entry.color};
        entry.frameBuffer = Miren::createFrameBuffer(Miren::FrameBufferSpecification(attachments, 1));
        entry.live = true;
        // A material preview is an FBO: GL rows go bottom to top, D3D/Metal rows — top to bottom.
        lease->flipVertical = Foundation::ACTIVE_GRAPHICS_TRAITS.originBottomLeft;
        lease->requested = true; // the first frame immediately, without waiting for the form's tick
    } else if (it->second.type == AssetType::TEXTURE) {
        AssetRef<TextureAsset> texture(assetId);
        if (!texture || !texture->getMirenHandle().isValid()) { return nullptr; }
        entry.pinned = texture;
        lease->plate = true; // a regular texture (not an FBO) — no flip, on a square plate
        // An SRGB texture cannot be given to the UI as is: the sample is decoded to linear by
        // hardware, while the UI is display-space, so the image would darken. update() builds the
        // display copy (a blit with encode).
        if (texture->getFormat() == Miren::TextureFormat::RGBA8_SRGB) {
            entry.displayCopyPending = true;
        } else {
            const Size size = texture->getSize();
            lease->texture = m_wrap(texture->getMirenHandle(), PandaUI::Size(size.width, size.height));
        }
    } else {
        return nullptr;
    }
    entry.lease = lease;
    m_previews.push_back(std::move(entry));
    return lease;
}

bool ThumbnailsManager::renderPendingDisplayCopy() {
    for (PreviewEntry &entry : m_previews) {
        if (!entry.displayCopyPending) { continue; }
        entry.displayCopyPending = false;
        const std::shared_ptr<AssetPreview> lease = entry.lease.lock();
        TextureAsset *texture = entry.pinned.get();
        if (!lease || texture == nullptr) { continue; }
        entry.displayCopy = renderDisplayCopy(*texture);
        const Size size = texture->getSize();
        lease->texture = m_wrap(
            entry.displayCopy.isValid() ? entry.displayCopy : texture->getMirenHandle(),
            PandaUI::Size(size.width, size.height)
        );
        return true;
    }
    return false;
}

void ThumbnailsManager::releasePreview(PreviewEntry &entry) {
    // The lease may still be alive (project closing): the form sees an empty texture and hides the
    // preview. GPU resources outlive the current frame — the old handle may still be in the draw
    // list.
    if (const std::shared_ptr<AssetPreview> lease = entry.lease.lock()) { lease->texture = PandaUI::TextureHandle(); }
    if (entry.frameBuffer.isValid()) { m_retiredFrameBuffers.push_back(entry.frameBuffer); }
    if (entry.color.isValid()) { m_retiredTextures.push_back(entry.color); }
    if (entry.displayCopy.isValid()) { m_retiredTextures.push_back(entry.displayCopy); }
    entry.pinned.reset(); // released — the texture may be unloaded
    entry = PreviewEntry{};
}

PandaUI::TextureHandle ThumbnailsManager::thumbnailFor(AssetId materialId) {
    auto it = m_thumbnails.find(materialId);
    if (it != m_thumbnails.end()) { return it->second.ui; }
    enqueue(materialId);
    return PandaUI::TextureHandle();
}

void ThumbnailsManager::prefetch(AssetId materialId) {
    if (m_thumbnails.contains(materialId)) { return; }
    enqueue(materialId);
}

void ThumbnailsManager::enqueue(AssetId materialId) {
    if (std::find(m_queue.begin(), m_queue.end(), materialId) != m_queue.end()) { return; }
    m_queue.push_back(materialId);
}

void ThumbnailsManager::cancelPending() {
    m_queue.clear();
}

void ThumbnailsManager::invalidate(AssetId materialId) {
    // The texture is NOT deleted and NOT evicted from the cache: its handle is held by live grid
    // cells (CollectionView does not reconfigure visible cells itself — binding a deleted texture
    // crashed GL). Just re-render into the SAME texture — the thumbnail updates "live".
    if (m_thumbnails.contains(materialId)) { enqueue(materialId); }
}

void ThumbnailsManager::clear() {
    m_queue.clear();
    // The project assets die — previews must hold neither references nor ids.
    for (PreviewEntry &entry : m_previews) { releasePreview(entry); }
    m_previews.clear();
    for (auto &entry : m_thumbnails) { Miren::deleteTexture(entry.second.native); }
    m_thumbnails.clear();
}

void ThumbnailsManager::ensureHdrScratch() {
    if (m_hasHdrScratch) { return; }
    Miren::TextureCreate create;
    create.m_minFiltering = Miren::LINEAR;
    create.m_magFiltering = Miren::LINEAR;
    create.m_wrap = Miren::CLAMP;
    create.m_width = THUMBNAIL_SIZE;
    create.m_height = THUMBNAIL_SIZE;
    create.m_format = Miren::TextureFormat::RGBA16F;
    m_hdrColor = Miren::createTexture(create);
    create.m_format = Miren::TextureFormat::DEPTH24STENCIL8;
    m_hdrDepth = Miren::createTexture(create);
    Miren::FrameBufferAttachment attachments[] = {m_hdrColor, m_hdrDepth};
    m_hdrFbSpecification = Miren::FrameBufferSpecification(attachments, 2);
    m_hdrFrameBuffer = Miren::createFrameBuffer(m_hdrFbSpecification);
    Miren::setViewport(Views::THUMBNAIL_RENDER_VIEW, Miren::Rect(0, 0, THUMBNAIL_SIZE, THUMBNAIL_SIZE));
    Miren::setViewClear(Views::THUMBNAIL_RENDER_VIEW, 0x00000000);
    Miren::setViewFrameBuffer(Views::THUMBNAIL_RENDER_VIEW, m_hdrFrameBuffer);
    Miren::setViewport(Views::THUMBNAIL_TONEMAP_VIEW, Miren::Rect(0, 0, THUMBNAIL_SIZE, THUMBNAIL_SIZE));
    Miren::setViewClear(Views::THUMBNAIL_TONEMAP_VIEW, 0x00000000);
    m_hasHdrScratch = true;
}

void ThumbnailsManager::ensureBlitQuad() {
    if (m_hasBlitQuad) { return; }
    Miren::VertexBufferLayoutData layoutData;
    layoutData.pushVec3(); // position (NDC)
    layoutData.pushVec2(); // uv
    Miren::VertexLayoutHandle layout = Miren::createVertexLayout(layoutData);
    // Like the postprocessing fullscreen quad: the bottom row of the source is v=0 in GL,
    // v=1 in D3D/Metal.
    constexpr float bottomV = Foundation::ACTIVE_GRAPHICS_TRAITS.originBottomLeft ? 0.f : 1.f;
    constexpr float topV = 1.f - bottomV;
    // clang-format off
    const float vertices[20] = {
        -1.f, -1.f, 0.f, 0.f, bottomV,
         1.f, -1.f, 0.f, 1.f, bottomV,
         1.f,  1.f, 0.f, 1.f, topV,
        -1.f,  1.f, 0.f, 0.f, topV,
    };
    // clang-format on
    const uint32_t indices[6] = {0, 1, 2, 2, 3, 0};
    m_blitVertexBuffer = Miren::createVertexBuffer(
        Foundation::Memory::copying(vertices, sizeof(vertices)), sizeof(vertices), layout
    );
    m_blitIndexBuffer = Miren::createIndexBuffer(
        Foundation::Memory::copying(indices, sizeof(indices)), Miren::BufferElementType::UnsignedInt, 6
    );
    m_hasBlitQuad = true;
}

Miren::ProgramHandle ThumbnailsManager::blitProgram() {
    if (!m_blitProgram.isValid()) {
        m_blitProgram = Miren::createProgram(AssetImporterBase::loadShaderProgram(
            "default-shaders/postprocess.vert", "editor-shaders/preview_blit.frag"
        ));
    }
    return m_blitProgram;
}

void ThumbnailsManager::renderTextureThumbnail(AssetId textureId) {
    Miren::ProgramHandle shader = blitProgram();
    if (!shader.isValid()) { return; }
    // The AssetRef is local: after the pass the refcount drops and the full-size texture may be
    // unloaded — only the square thumbnail stays in the cache.
    AssetRef<TextureAsset> texture(textureId);
    if (!texture) { return; }
    Miren::TextureHandle source = texture->getMirenHandle();
    if (!source.isValid()) { return; }
    ensureBlitQuad();

    // Fit the source frame into a square: the long side in full, the short side gets margins.
    const Size size = texture->getSize();
    const float aspect = (size.height > 0.f) ? (size.width / size.height) : 1.f;
    Std140Buffer fit;
    if (aspect >= 1.f) {
        fit.addVec4(1.f, aspect, 0.f, -(aspect - 1.f) * 0.5f);
    } else {
        const float inv = 1.f / aspect;
        fit.addVec4(inv, 1.f, -(inv - 1.f) * 0.5f, 0.f);
    }
    // An SRGB source is sampled as linear — the thumbnail is display-referred: inverse encode.
    fit.addVec4(texture->getFormat() == Miren::TextureFormat::RGBA8_SRGB ? 1.f : 0.f, 0.f, 0.f, 0.f);

    Miren::setViewClear(Views::THUMBNAIL_TONEMAP_VIEW, 0x00000000);
    Miren::setViewport(Views::THUMBNAIL_TONEMAP_VIEW, Miren::Rect(0, 0, THUMBNAIL_SIZE, THUMBNAIL_SIZE));
    Miren::setState(0); // no depth/cull: a flat quad over the whole target
    Miren::addInputTexture("inputTexture", source);
    Miren::addInputUniformBuffer("BLIT_FIT", fit.getData(), fit.getSize());
    Miren::setVertexBuffer(m_blitVertexBuffer);
    Miren::setIndexBuffer(m_blitIndexBuffer, 0, 6);
    Miren::submit(Views::THUMBNAIL_TONEMAP_VIEW, shader);
}

Miren::TextureHandle ThumbnailsManager::renderDisplayCopy(TextureAsset &texture) {
    Miren::ProgramHandle shader = blitProgram();
    Miren::TextureHandle source = texture.getMirenHandle();
    if (!shader.isValid() || !source.isValid()) { return {}; }
    ensureBlitQuad();

    const Size size = texture.getSize();
    Miren::TextureCreate create;
    create.m_minFiltering = Miren::LINEAR;
    create.m_magFiltering = Miren::LINEAR;
    create.m_wrap = Miren::CLAMP;
    create.m_format = Miren::TextureFormat::RGBA8;
    create.m_width = static_cast<uint16_t>(size.width);
    create.m_height = static_cast<uint16_t>(size.height);
    Miren::TextureHandle copy = Miren::createTexture(create);
    if (!copy.isValid()) { return {}; }

    Miren::FrameBufferAttachment attachments[] = {copy};
    Miren::FrameBufferSpecification specification(attachments, 1);
    Miren::FrameBufferHandle frameBuffer = Miren::createFrameBuffer(specification);
    Miren::setViewFrameBuffer(Views::THUMBNAIL_TONEMAP_VIEW, frameBuffer);
    Miren::setViewClear(Views::THUMBNAIL_TONEMAP_VIEW, 0x00000000);
    Miren::setViewport(
        Views::THUMBNAIL_TONEMAP_VIEW,
        Miren::Rect(0, 0, static_cast<int>(size.width), static_cast<int>(size.height))
    );

    // Frame 1:1, no letterbox; encode — the source is guaranteed to be SRGB (the caller decides).
    Std140Buffer fit;
    fit.addVec4(1.f, 1.f, 0.f, 0.f);
    fit.addVec4(1.f, 0.f, 0.f, 0.f);

    Miren::setState(0); // no depth/cull: a flat quad over the whole target
    Miren::addInputTexture("inputTexture", source);
    Miren::addInputUniformBuffer("BLIT_FIT", fit.getData(), fit.getSize());
    Miren::setVertexBuffer(m_blitVertexBuffer);
    Miren::setIndexBuffer(m_blitIndexBuffer, 0, 6);
    Miren::submit(Views::THUMBNAIL_TONEMAP_VIEW, shader);

    retireFrameBuffer(frameBuffer); // the FBO is needed only for the duration of the pass
    return copy;
}

void ThumbnailsManager::retireFrameBuffer(Miren::FrameBufferHandle frameBuffer) {
    m_retiredFrameBuffers.push_back(frameBuffer);
}

void ThumbnailsManager::renderNextThumbnail() {
    if (m_queue.empty()) { return; }
    AssetId materialId = m_queue.front();
    m_queue.pop_front();
    auto *handler = static_cast<AssetHandlerEditor *>(GameContext::getAssetHandler());
    if (!handler) { return; }
    // A type guard, not just an existence check: AssetRef<MaterialAsset> on a foreign id resolves
    // an object of ANOTHER class (an unchecked cast) — getShaderAsset would read garbage. The queue
    // is public (panels call thumbnailFor/prefetch) — a non-material is silently dropped.
    const auto it = handler->getRegistry().find(materialId);
    // A type guard, not just an existence check (see above): materials (sphere) and textures (blit)
    // are drawn.
    if (it == handler->getRegistry().end()) { return; }
    const AssetType assetType = it->second.type;
    const bool isTexture = assetType == AssetType::TEXTURE;
    if (assetType != AssetType::MATERIAL && !isTexture) { return; }
    ensureRenderer();
    if (!isTexture) {
        ensureHdrScratch();
        // The sphere goes into the shared HDR scratch target (linear light), the thumbnail's
        // display texture is produced via Tonemap. The material is not ready (the shader is not
        // built yet) — no thumbnail is built: the scratch target holds the sphere of the PREVIOUS
        // material, which would be cached under this id. The cell repeats the request.
        if (!m_renderer->renderToView(AssetRef<MaterialAsset>(materialId), Views::THUMBNAIL_RENDER_VIEW)) { return; }
    }

    // An update of an existing thumbnail is rendered INTO THE SAME texture: its handle may be held
    // by live grid cells, the id must stay valid (and the image updates without a UI rebind).
    Miren::TextureHandle color;
    const auto existing = m_thumbnails.find(materialId);
    const bool isNew = existing == m_thumbnails.end();
    if (isNew) {
        Miren::TextureCreate create;
        create.m_minFiltering = Miren::LINEAR;
        create.m_magFiltering = Miren::LINEAR;
        create.m_wrap = Miren::CLAMP;
        create.m_format = Miren::TextureFormat::RGBA8;
        create.m_width = THUMBNAIL_SIZE;
        create.m_height = THUMBNAIL_SIZE;
        color = Miren::createTexture(create);
    } else {
        color = existing->second.native;
    }
    Miren::FrameBufferAttachment attachments[] = {color};
    Miren::FrameBufferSpecification specification(attachments, 1);
    Miren::FrameBufferHandle frameBuffer = Miren::createFrameBuffer(specification);
    Miren::setViewFrameBuffer(Views::THUMBNAIL_TONEMAP_VIEW, frameBuffer);
    if (isTexture) {
        renderTextureThumbnail(materialId);
    } else {
        m_renderer->finalizeToView(m_hdrColor, THUMBNAIL_SIZE, Views::THUMBNAIL_TONEMAP_VIEW);
    }

    // The thumbnail does not need the FBO after finalization — it is deleted in the next update().
    retireFrameBuffer(frameBuffer);

    if (isNew) {
        Thumbnail thumbnail;
        thumbnail.native = color;
        thumbnail.ui = m_wrap(color, PandaUI::Size(THUMBNAIL_SIZE, THUMBNAIL_SIZE));
        m_thumbnails[materialId] = thumbnail;
    }
    if (m_onThumbnailReady) { m_onThumbnailReady(materialId); }
}

} // namespace Panda
