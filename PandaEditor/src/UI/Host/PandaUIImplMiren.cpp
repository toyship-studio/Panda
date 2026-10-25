#include "PandaUIImplMiren.hpp"

#include <Miren/Miren.hpp>
#include <Panda/Assets/Base/AssetImporterBase.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace {

Miren::Rect toMirenScissor(const PandaUI::Rect &rect, PandaUI::Size viewportSize, PandaUI::Size dpiScale) {
    if (rect.size.width <= 0.f || rect.size.height <= 0.f || viewportSize.width <= 0.f || viewportSize.height <= 0.f) {
        return Miren::Rect::zero();
    }

    const int viewportWidth = static_cast<int>(std::ceil(viewportSize.width * dpiScale.width));
    const int viewportHeight = static_cast<int>(std::ceil(viewportSize.height * dpiScale.height));
    int x0 = static_cast<int>(std::floor(rect.origin.x * dpiScale.width));
    int x1 = static_cast<int>(std::ceil((rect.origin.x + rect.size.width) * dpiScale.width));
    int y0 = static_cast<int>(std::floor((viewportSize.height - rect.origin.y - rect.size.height) * dpiScale.height));
    int y1 = static_cast<int>(std::ceil((viewportSize.height - rect.origin.y) * dpiScale.height));

    x0 = std::clamp(x0, 0, viewportWidth);
    x1 = std::clamp(x1, 0, viewportWidth);
    y0 = std::clamp(y0, 0, viewportHeight);
    y1 = std::clamp(y1, 0, viewportHeight);
    if (x1 <= x0 || y1 <= y0) { return Miren::Rect::zero(); }
    return Miren::Rect(x0, y0, x1 - x0, y1 - y0);
}

} // namespace

struct PandaUIImplMiren::Data {
    Miren::ProgramHandle shader;
    Miren::ProgramHandle sdfShader; // analytic rounded rectangles (RoundedSDF)
    Miren::VertexLayoutHandle vertexLayout;
    Miren::TextureHandle whiteTexture;
    std::unordered_map<uint64_t, Miren::TextureHandle> textures;
    std::unordered_set<uint64_t> external; // wrapped foreign textures (engine FBOs) — not deleted on shutdown
    uint64_t nextTextureId = 1;
    bool initialized = false;
};

PandaUIImplMiren::PandaUIImplMiren()
    : m_data(std::make_unique<Data>()) {}

PandaUIImplMiren::~PandaUIImplMiren() {
    shutdown();
}

void PandaUIImplMiren::init() {
    if (m_data->initialized) { return; }

    m_data->shader =
        Miren::createProgram(Panda::AssetImporterBase::loadShaderProgram("panda-ui/ui.vert", "panda-ui/ui.frag"));
    m_data->sdfShader = Miren::createProgram(
        Panda::AssetImporterBase::loadShaderProgram("panda-ui/ui_sdf.vert", "panda-ui/ui_sdf.frag")
    );

    Miren::VertexBufferLayoutData layoutData;
    layoutData.pushVec3(); // position
    layoutData.pushVec2(); // texCoord / localPos
    layoutData.pushVec4(); // color
    layoutData.pushVec4(); // sdfParams (half-size, radius, outline width)
    m_data->vertexLayout = Miren::createVertexLayout(layoutData);

    const unsigned char whitePixel[] = {255, 255, 255, 255};
    PandaUI::TextureHandle white = createTextureRGBA8(whitePixel, 1, 1);
    m_data->whiteTexture = m_data->textures[white.id];
    m_data->initialized = true;
}

void PandaUIImplMiren::shutdown() {
    if (!m_data->initialized) { return; }
    if (m_data->shader.isValid()) { Miren::deleteProgram(m_data->shader); }
    if (m_data->sdfShader.isValid()) { Miren::deleteProgram(m_data->sdfShader); }
    if (m_data->vertexLayout.isValid()) { Miren::deleteVertexLayout(m_data->vertexLayout); }
    for (auto &entry : m_data->textures) {
        const bool owned = m_data->external.find(entry.first) == m_data->external.end();
        if (owned && entry.second.isValid()) { Miren::deleteTexture(entry.second); } // leave foreign (FBO) ones alone
    }
    m_data->textures.clear();
    m_data->external.clear();
    m_data->whiteTexture = {};
    m_data->initialized = false;
}

PandaUI::TextureHandle
PandaUIImplMiren::createTextureRGBA8(const unsigned char *pixels, uint16_t width, uint16_t height) {
    if (!pixels || width == 0 || height == 0) { return {}; }

    Miren::TextureCreate create;
    create.m_format = Miren::TextureFormat::RGBA8;
    create.m_width = width;
    create.m_height = height;
    create.m_wrap = Miren::TextureWrapMode::CLAMP;
    create.m_minFiltering = Miren::TextureFiltering::LINEAR;
    create.m_magFiltering = Miren::TextureFiltering::LINEAR;
    create.m_data =
        Foundation::Memory::copying(pixels, static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 4);
    Miren::TextureHandle nativeTexture = Miren::createTexture(create);

    PandaUI::TextureHandle texture;
    texture.id = m_data->nextTextureId++;
    texture.size = PandaUI::Size(static_cast<float>(width), static_cast<float>(height));
    m_data->textures[texture.id] = nativeTexture;
    return texture;
}

PandaUI::TextureHandle PandaUIImplMiren::createTextureFromDecoded(Miren::TextureCreate create) {
    if (!create.m_data.data || create.m_width == 0 || create.m_height == 0) { return {}; }
    create.m_wrap = Miren::TextureWrapMode::CLAMP;
    create.m_minFiltering = Miren::TextureFiltering::LINEAR;
    create.m_magFiltering = Miren::TextureFiltering::LINEAR;
    Miren::TextureHandle nativeTexture = Miren::createTexture(create);

    PandaUI::TextureHandle texture;
    texture.id = m_data->nextTextureId++;
    texture.size = PandaUI::Size(static_cast<float>(create.m_width), static_cast<float>(create.m_height));
    m_data->textures[texture.id] = nativeTexture;
    return texture;
}

bool PandaUIImplMiren::replaceTexture(PandaUI::TextureHandle handle, Miren::TextureCreate create) {
    auto it = m_data->textures.find(handle.id);
    if (it == m_data->textures.end() || !create.m_data.data || create.m_width == 0 || create.m_height == 0) { return false; }
    create.m_wrap = Miren::TextureWrapMode::CLAMP;
    create.m_minFiltering = Miren::TextureFiltering::LINEAR;
    create.m_magFiltering = Miren::TextureFiltering::LINEAR;
    Miren::TextureHandle native = Miren::createTexture(create);
    if (!native.isValid()) { return false; }
    const bool owned = m_data->external.find(handle.id) == m_data->external.end();
    if (owned && it->second.isValid()) { Miren::deleteTexture(it->second); }
    m_data->external.erase(handle.id);
    it->second = native;
    return true;
}

PandaUI::TextureHandle PandaUIImplMiren::createTextureFromStaticResource(const std::filesystem::path &path) {
    return createTextureFromDecoded(Panda::AssetImporterBase::load2DTexture(path));
}

PandaUI::TextureHandle PandaUIImplMiren::wrapNativeTexture(Miren::TextureHandle native, PandaUI::Size size) {
    if (!native.isValid()) { return {}; }
    PandaUI::TextureHandle texture;
    texture.id = m_data->nextTextureId++;
    texture.size = size;
    m_data->textures[texture.id] = native;
    m_data->external.insert(texture.id); // the owner is the engine: shutdown does NOT delete it
    return texture;
}

void PandaUIImplMiren::render(const PandaUI::DrawList &drawList, uint16_t viewId) {
    if (!m_data->initialized || drawList.vertices.empty() || drawList.indices.empty() || drawList.commands.empty()) {
        return;
    }

    Miren::TransientVertexBuffer tvb;
    const uint32_t vbSize = static_cast<uint32_t>(drawList.vertices.size() * sizeof(PandaUI::DrawVertex));
    Miren::allocTransientVertexBuffer(&tvb, vbSize);
    std::memcpy(tvb.data, drawList.vertices.data(), vbSize);

    Miren::TransientIndexBuffer tib;
    Miren::allocTransientIndexBuffer(&tib, static_cast<uint32_t>(drawList.indices.size()));
    std::memcpy(tib.data, drawList.indices.data(), drawList.indices.size() * sizeof(uint32_t));

    for (const PandaUI::DrawCommand &command : drawList.commands) {
        const bool isSdf = command.primitive == PandaUI::DrawPrimitive::RoundedSDF;
        const Miren::ProgramHandle program = isSdf ? m_data->sdfShader : m_data->shader;

        Miren::TextureHandle texture;
        if (!isSdf) {
            texture = m_data->whiteTexture;
            auto textureIt = m_data->textures.find(command.texture.id);
            if (textureIt != m_data->textures.end()) { texture = textureIt->second; }
            if (!texture.isValid()) { continue; }
        }

        Miren::setState(MIREN_STATE_BLEND);
        Miren::addInputUniformBuffer("UBO", &drawList.viewProjection[0][0], sizeof(glm::mat4));
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
        Miren::submit(viewId, program);
    }
}
