#include "Panda/UI/PandaUIMirenRenderer.hpp"

#include "Panda/Assets/Base/AssetImporterBase.hpp"

#include <Foundation/Profile.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace Panda {

namespace {

    // The clip of a command in the pixels of the target: PandaUI counts points from the top left,
    // Miren pixels from the bottom left.
    Miren::Rect toMirenScissor(const PandaUI::Rect &rect, PandaUI::Size viewportSize, PandaUI::Size dpiScale) {
        if (rect.size.width <= 0.f || rect.size.height <= 0.f || viewportSize.width <= 0.f ||
            viewportSize.height <= 0.f) {
            return Miren::Rect::zero();
        }
        const int viewportWidth = static_cast<int>(std::ceil(viewportSize.width * dpiScale.width));
        const int viewportHeight = static_cast<int>(std::ceil(viewportSize.height * dpiScale.height));
        int x0 = static_cast<int>(std::floor(rect.origin.x * dpiScale.width));
        int x1 = static_cast<int>(std::ceil((rect.origin.x + rect.size.width) * dpiScale.width));
        int y0 =
            static_cast<int>(std::floor((viewportSize.height - rect.origin.y - rect.size.height) * dpiScale.height));
        int y1 = static_cast<int>(std::ceil((viewportSize.height - rect.origin.y) * dpiScale.height));
        x0 = std::clamp(x0, 0, viewportWidth);
        x1 = std::clamp(x1, 0, viewportWidth);
        y0 = std::clamp(y0, 0, viewportHeight);
        y1 = std::clamp(y1, 0, viewportHeight);
        if (x1 <= x0 || y1 <= y0) { return Miren::Rect::zero(); }
        return Miren::Rect(x0, y0, x1 - x0, y1 - y0);
    }

    // What the UI asks of a texture, whatever its pixels are.
    void applySampling(Miren::TextureCreate &create) {
        create.m_wrap = Miren::TextureWrapMode::CLAMP;
        create.m_minFiltering = Miren::TextureFiltering::LINEAR;
        create.m_magFiltering = Miren::TextureFiltering::LINEAR;
    }

} // namespace

struct PandaUIMirenRenderer::Data {
    Miren::ProgramHandle shader;
    Miren::ProgramHandle sdfShader; // analytic rounded rectangles (RoundedSDF)
    Miren::VertexLayoutHandle vertexLayout;
    Miren::TextureHandle whiteTexture;
    std::unordered_map<uint64_t, Miren::TextureHandle> textures;
    std::unordered_set<uint64_t> wrapped; // someone else's textures: forgotten, never deleted
    uint64_t nextTextureId = 1;
    bool initialized = false;

    PandaUI::TextureHandle add(Miren::TextureHandle native, PandaUI::Size size) {
        PandaUI::TextureHandle texture;
        texture.id = nextTextureId++;
        texture.size = size;
        textures[texture.id] = native;
        return texture;
    }
    void release(uint64_t id, Miren::TextureHandle native) {
        if (wrapped.erase(id) == 0 && native.isValid()) { Miren::deleteTexture(native); }
    }
};

PandaUIMirenRenderer::PandaUIMirenRenderer()
    : m_data(std::make_unique<Data>()) {}

PandaUIMirenRenderer::~PandaUIMirenRenderer() {
    shutdown();
}

void PandaUIMirenRenderer::init() {
    if (m_data->initialized) { return; }
    m_data->shader = Miren::createProgram(AssetImporterBase::loadShaderProgram("panda-ui/ui.vert", "panda-ui/ui.frag"));
    m_data->sdfShader =
        Miren::createProgram(AssetImporterBase::loadShaderProgram("panda-ui/ui_sdf.vert", "panda-ui/ui_sdf.frag"));

    Miren::VertexBufferLayoutData layoutData;
    layoutData.pushVec3(); // position
    layoutData.pushVec2(); // texCoord / localPos
    layoutData.pushVec4(); // color
    layoutData.pushVec4(); // sdfParams (half-size, radius, outline width)
    m_data->vertexLayout = Miren::createVertexLayout(layoutData);

    const uint8_t whitePixel[] = {255, 255, 255, 255};
    m_data->whiteTexture = m_data->textures[createTextureRGBA8(whitePixel, 1, 1).id];
    m_data->initialized = true;
}

void PandaUIMirenRenderer::shutdown() {
    if (!m_data->initialized) { return; }
    if (m_data->shader.isValid()) { Miren::deleteProgram(m_data->shader); }
    if (m_data->sdfShader.isValid()) { Miren::deleteProgram(m_data->sdfShader); }
    if (m_data->vertexLayout.isValid()) { Miren::deleteVertexLayout(m_data->vertexLayout); }
    for (auto &[id, native] : m_data->textures) {
        if (!m_data->wrapped.contains(id) && native.isValid()) { Miren::deleteTexture(native); }
    }
    *m_data = Data{};
}

PandaUI::TextureHandle
PandaUIMirenRenderer::createTextureRGBA8(const uint8_t *pixels, uint16_t width, uint16_t height) {
    if (!pixels || width == 0 || height == 0) { return {}; }
    Miren::TextureCreate create;
    create.m_format = Miren::TextureFormat::RGBA8;
    create.m_width = width;
    create.m_height = height;
    create.m_data =
        Foundation::Memory::copying(pixels, static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 4);
    return createTexture(std::move(create));
}

PandaUI::TextureHandle PandaUIMirenRenderer::createTexture(Miren::TextureCreate create) {
    if (!create.m_data.data || create.m_width == 0 || create.m_height == 0) { return {}; }
    applySampling(create);
    const PandaUI::Size size(static_cast<float>(create.m_width), static_cast<float>(create.m_height));
    return m_data->add(Miren::createTexture(create), size);
}

PandaUI::TextureHandle PandaUIMirenRenderer::createTextureFromStaticResource(const std::filesystem::path &path) {
    return createTexture(AssetImporterBase::load2DTexture(path));
}

PandaUI::TextureHandle PandaUIMirenRenderer::wrapNativeTexture(Miren::TextureHandle native, PandaUI::Size size) {
    if (!native.isValid()) { return {}; }
    const PandaUI::TextureHandle texture = m_data->add(native, size);
    m_data->wrapped.insert(texture.id);
    return texture;
}

bool PandaUIMirenRenderer::replaceTexture(PandaUI::TextureHandle handle, Miren::TextureCreate create) {
    auto it = m_data->textures.find(handle.id);
    if (it == m_data->textures.end() || !create.m_data.data || create.m_width == 0 || create.m_height == 0) {
        return false;
    }
    applySampling(create);
    const Miren::TextureHandle native = Miren::createTexture(create);
    if (!native.isValid()) { return false; }
    m_data->release(handle.id, it->second);
    it->second = native;
    return true;
}

void PandaUIMirenRenderer::destroyTexture(PandaUI::TextureHandle handle) {
    auto it = m_data->textures.find(handle.id);
    if (it == m_data->textures.end()) { return; }
    m_data->release(handle.id, it->second);
    m_data->textures.erase(it);
}

void PandaUIMirenRenderer::render(const PandaUI::DrawListView &drawList, Miren::ViewId view) {
    PANDA_PROFILE_SCOPE_N("PandaUIMirenRenderer::render");
    if (!m_data->initialized || drawList.verticesCount == 0 || drawList.indicesCount == 0 ||
        drawList.commandsCount == 0) {
        return;
    }

    Miren::TransientVertexBuffer tvb;
    const uint32_t vbSize = drawList.verticesCount * static_cast<uint32_t>(sizeof(PandaUI::DrawVertex));
    Miren::allocTransientVertexBuffer(&tvb, vbSize);
    std::memcpy(tvb.data, drawList.vertices, vbSize);

    Miren::TransientIndexBuffer tib;
    Miren::allocTransientIndexBuffer(&tib, drawList.indicesCount);
    std::memcpy(tib.data, drawList.indices, drawList.indicesCount * sizeof(uint32_t));

    for (uint32_t i = 0; i < drawList.commandsCount; ++i) {
        const PandaUI::DrawCommand &command = drawList.commands[i];
        const bool isSdf = command.primitive == PandaUI::DrawPrimitive::RoundedSDF;
        const Miren::ProgramHandle program = isSdf ? m_data->sdfShader : m_data->shader;

        Miren::TextureHandle texture;
        if (!isSdf) {
            texture = m_data->whiteTexture; // a handle that was destroyed draws white
            auto textureIt = m_data->textures.find(command.texture.id);
            if (textureIt != m_data->textures.end()) { texture = textureIt->second; }
            if (!texture.isValid()) { continue; }
        }

        Miren::setState(MIREN_STATE_BLEND);
        Miren::addInputUniformBuffer("UBO", &drawList.viewProjection[0][0], sizeof(glm::mat4));
        // The rounded clip of the command (zero when none): the shaders fade the pixels outside it.
        Miren::addInputUniformBuffer("MASK", &command.mask, sizeof(PandaUI::DrawCommand::Mask));
        if (!isSdf) { Miren::addInputTexture("u_texture", texture); }
        Miren::setVertexLayout(m_data->vertexLayout);
        Miren::setVertexBuffer(tvb.handle, tvb.startVertex);
        Miren::setIndexBuffer(
            tib.handle,
            tib.startIndex + static_cast<intptr_t>(command.indexOffset * sizeof(uint32_t)),
            command.indexCount
        );
        if (command.hasClip) {
            Miren::setScissorRect(toMirenScissor(command.clipRect, drawList.viewportSize, drawList.dpiScale));
        }
        Miren::submit(view, program);
    }
}

} // namespace Panda
