// Adaptation of PandaUI to the Miren renderer: UI/SDF shaders, textures, binding of the window
// views, scissor and draw list submit.
#include "PandaUIInternal.hpp"

#include "Panda/Assets/Base/AssetImporterBase.hpp"

#include <Foundation/Profile.hpp>
#include <Fern/Window/Window.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Panda {

namespace PandaUIInternal {

    static std::unordered_map<uint64_t, Miren::TextureHandle> s_textures;
    static uint64_t s_nextTextureId = 1;
    static Miren::ProgramHandle s_shader;
    static Miren::ProgramHandle s_sdfShader; // analytic rounded rectangles (RoundedSDF)
    static Miren::VertexLayoutHandle s_vertexLayout;
    static Miren::TextureHandle s_whiteTexture;

    static Miren::Rect toMirenScissor(const PandaUI::Rect &rect, PandaUI::Size viewportSize, PandaUI::Size dpiScale) {
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

    static Miren::TextureHandle createNativeTextureRGBA8(const uint8_t *pixels, uint16_t width, uint16_t height) {
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
        return Miren::createTexture(create);
    }

    void initializeRenderer() {
        s_shader = Miren::createProgram(AssetImporterBase::loadShaderProgram("panda-ui/ui.vert", "panda-ui/ui.frag"));
        s_sdfShader =
            Miren::createProgram(AssetImporterBase::loadShaderProgram("panda-ui/ui_sdf.vert", "panda-ui/ui_sdf.frag"));

        Miren::VertexBufferLayoutData layoutData;
        layoutData.pushVec3(); // position
        layoutData.pushVec2(); // texCoord / localPos
        layoutData.pushVec4(); // color
        layoutData.pushVec4(); // sdfParams (half-size, radius, outline width)
        s_vertexLayout = Miren::createVertexLayout(layoutData);

        const uint8_t whitePixel[] = {255, 255, 255, 255};
        s_whiteTexture = createNativeTextureRGBA8(whitePixel, 1, 1);
    }

    void shutdownRenderer() {
        if (s_shader.isValid()) { Miren::deleteProgram(s_shader); }
        if (s_sdfShader.isValid()) { Miren::deleteProgram(s_sdfShader); }
        if (s_vertexLayout.isValid()) { Miren::deleteVertexLayout(s_vertexLayout); }
        if (s_whiteTexture.isValid()) { Miren::deleteTexture(s_whiteTexture); }
        for (auto &entry : s_textures) {
            if (entry.second.isValid()) { Miren::deleteTexture(entry.second); }
        }

        s_shader = {};
        s_sdfShader = {};
        s_vertexLayout = {};
        s_whiteTexture = {};
        s_textures.clear();
        s_nextTextureId = 1;
    }

    void syncViewport(PandaUIWindowEntry &entry) {
        if (!entry.window) { return; }

        PandaUI::Size size = getEntrySize(entry);
        PandaUI::Size dpi = getEntryDpiScale(entry);
        Fern::GraphicsContext *context = entry.window->getDrawingContext();
        if (context) { context->update(); }
        Miren::setViewGraphicsContext(entry.viewId, context);
        Miren::setViewport(
            entry.viewId,
            Miren::Rect(0, 0, static_cast<int>(size.width * dpi.width), static_cast<int>(size.height * dpi.height))
        );
        if (entry.ownsNativeWindow) { Miren::setViewClear(entry.viewId, 0x111318ff); }
    }

    /* ---------------------------- renderer ui_* ABI ---------------------------- */

    static uint64_t apiCreateTextureRGBA8(const uint8_t *pixels, uint16_t width, uint16_t height) {
        Miren::TextureHandle texture = createNativeTextureRGBA8(pixels, width, height);
        if (!texture.isValid()) { return 0; }

        const uint64_t textureId = s_nextTextureId++;
        s_textures[textureId] = texture;
        return textureId;
    }

    static void apiDestroyTexture(uint64_t textureId) {
        auto it = s_textures.find(textureId);
        if (it == s_textures.end()) { return; }
        if (it->second.isValid()) { Miren::deleteTexture(it->second); }
        s_textures.erase(it);
    }

    static void apiSubmitDrawList(Handle windowHandle, const PandaUI::DrawListView *drawList) {
        PANDA_PROFILE_SCOPE_N("PandaUIImpl::submitDrawList");
        if (drawList) {
            PANDA_PROFILE_PLOT("PandaUI Draw Commands", static_cast<int64_t>(drawList->commandsCount));
            PANDA_PROFILE_PLOT("PandaUI Vertices", static_cast<int64_t>(drawList->verticesCount));
            PANDA_PROFILE_PLOT("PandaUI Indices", static_cast<int64_t>(drawList->indicesCount));
        }
        if (!drawList || drawList->verticesCount == 0 || drawList->indicesCount == 0 ||
            drawList->commandsCount == 0) {
            return;
        }

        PandaUIWindowEntry *entry = findWindow(windowHandle);
        if (!entry || !entry->window || !s_shader.isValid() || !s_vertexLayout.isValid()) { return; }

        Miren::TransientVertexBuffer tvb;
        const uint32_t vbSize = drawList->verticesCount * static_cast<uint32_t>(sizeof(PandaUI::DrawVertex));
        Miren::allocTransientVertexBuffer(&tvb, vbSize);
        std::memcpy(tvb.data, drawList->vertices, vbSize);

        Miren::TransientIndexBuffer tib;
        Miren::allocTransientIndexBuffer(&tib, drawList->indicesCount);
        std::memcpy(tib.data, drawList->indices, drawList->indicesCount * sizeof(uint32_t));

        for (uint32_t i = 0; i < drawList->commandsCount; ++i) {
            const PandaUI::DrawCommand &command = drawList->commands[i];
            const bool isSdf = command.primitive == PandaUI::DrawPrimitive::RoundedSDF;
            const Miren::ProgramHandle program = isSdf ? s_sdfShader : s_shader;

            Miren::TextureHandle texture;
            if (!isSdf) {
                texture = s_whiteTexture;
                auto textureIt = s_textures.find(command.texture.id);
                if (textureIt != s_textures.end()) { texture = textureIt->second; }
                if (!texture.isValid()) { continue; }
            }

            Miren::setState(MIREN_STATE_BLEND);
            Miren::addInputUniformBuffer("UBO", &drawList->viewProjection[0][0], sizeof(glm::mat4));
            if (!isSdf) { Miren::addInputTexture("u_texture", texture); }
            Miren::setVertexLayout(s_vertexLayout);
            Miren::setVertexBuffer(tvb.handle, tvb.startVertex);
            Miren::setIndexBuffer(
                tib.handle,
                tib.startIndex + static_cast<intptr_t>(command.indexOffset * sizeof(uint32_t)),
                command.indexCount
            );
            if (command.hasClip) {
                Miren::setScissorRect(toMirenScissor(command.clipRect, drawList->viewportSize, drawList->dpiScale));
            }
            Miren::submit(entry->viewId, program);
        }
    }

    void bindRendererHostApi(ScriptHostApi &hostApi) {
        hostApi.ui_CreateTextureRGBA8 = apiCreateTextureRGBA8;
        hostApi.ui_DestroyTexture = apiDestroyTexture;
        hostApi.ui_SubmitDrawList = apiSubmitDrawList;
    }

} // namespace PandaUIInternal

} // namespace Panda
