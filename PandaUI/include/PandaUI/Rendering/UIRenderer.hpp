#pragma once

#include "PandaUI/Core/Base.hpp"
#include "PandaUI/Rendering/DrawList.hpp"
#include "PandaUI/Rendering/Font.hpp"
#include "PandaUI/Rendering/PrimBuffer.hpp"
#include "PandaUI/Core/TextureHandle.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace PandaUI {

/// UIRenderer is responsible for drawing primitive two-dimensional shapes.
/// In the PandaUI core it only assembles the draw list; the actual rendering is done by the backend.
class UIRenderer final {
public:
    struct RectData {
        RectData()
            : center()
            , size()
            , color()
            , texture()
            , rotation(0)
            , textureCoords(0, 0, 1, 1)
            , transform(1.f) {}

        glm::vec3 center;
        Rect textureCoords;
        Size size;
        Color color;
        float rotation;
        glm::mat4 transform;
        TextureHandle texture;
    };

    struct ShadowData {
        Color color = Color(0.f, 0.f, 0.f, 0.f);
        Point offset;
        float radius = 0.f;
        float spread = 0.f;
    };

    struct Statistics {
        uint32_t drawCalls = 0;
        uint32_t primitiveCount = 0;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;

        uint32_t getTotalVertexCount() const {
            return vertexCount;
        }
        uint32_t getTotalIndexCount() const {
            return indexCount;
        }
    };

    UIRenderer();
    UIRenderer(UIRenderer &other) = delete;
    ~UIRenderer();
    void begin();
    void drawRect(const RectData &rect);
    // Rectangle with bilinear color interpolation across its 4 corners (for palettes/gradients).
    void drawGradientRect(Rect rect, Color topLeft, Color topRight, Color bottomRight, Color bottomLeft);
    // Filled triangle through three points (for disclosure arrows, indicators).
    void drawTriangle(Point a, Point b, Point c, Color color);
    // Line with round caps (thickness in pixels). Round caps give smooth joints at the
    // corners of a polyline — "chevrons"/arrows and other line icons are assembled from it.
    void drawLine(Point a, Point b, float thickness, Color color);
    void drawRoundedRect(const RectData &rect, float cornerRadius);
    void drawRoundedRectBorder(const RectData &rect, float cornerRadius, float borderWidth);
    void drawShadow(const RectData &rect, float cornerRadius, ShadowData shadow);
    void drawText(
        const FontAtlas &font, std::string_view text, Point position, Color color, float fontSize = 0.f,
        float letterSpacing = 0.f, bool italic = false
    );
    void end();
    void pushClipRect(Rect rect);
    void popClipRect();
    Statistics getStats();
    void setViewportSize(Size viewportSize, Size dpiScale = Size(1.f, 1.f));
    void setViewProj(glm::mat4 viewProj);
    const DrawList &getDrawList() const {
        return m_drawList;
    }
    UIRenderer &operator=(UIRenderer &other) = delete;

private:
    glm::mat4 m_viewProj;
    DrawList m_drawList;
    Statistics m_stats;
    TextureHandle m_currentTexture;
    DrawPrimitive m_currentPrimitive;
    uint32_t m_currentCommandIndexOffset;
    uint32_t m_currentCommandIndexCount;
    bool m_currentCommandHasClip;
    Rect m_currentCommandClipRect;
    Size m_viewportSize;
    Size m_dpiScale;
    std::vector<Rect> m_clipStack;
    Rect m_currentClipRect;
    bool m_hasCurrentClip;
    bool m_currentClipIsEmpty;
    TextureHandle m_solidTexture;
    Rect m_solidTextureCoords;

    void reset();
    void flush();
    void updateSolidTexture();
    void syncCurrentBatchState(TextureHandle texture, DrawPrimitive primitive);

    // Opens a primitive in the current batch: checks the clip, synchronizes the state
    // (texture/clip/kind), updates the statistics. Invalid if everything is clipped away.
    PrimBuffer beginPrimitive(TextureHandle texture, DrawPrimitive primitive = DrawPrimitive::Textured);
    // One more primitive in an already open batch — without re-synchronization (for
    // hot loops such as text glyphs, where the texture and clip are known not to change).
    PrimBuffer continuePrimitive();
    // If the rectangle has no texture of its own — substitutes the solid pixel for the fill.
    void resolveSolidTexture(RectData &rect) const;
    // Filled circle as a fan (segments<=0 — pick by the radius). For round line caps.
    void fillCircle(Point center, float radius, Color color, int segments);

    void drawRect(const glm::mat4 &transform, RectData rect);
    void drawRoundedRect(const glm::mat4 &transform, RectData rect, float cornerRadius);
    void drawRoundedRectBorder(const glm::mat4 &transform, RectData rect, float cornerRadius, float borderWidth);
    // A single quad with SDF parameters (RoundedSDF): an analytic rounded rectangle.
    // borderWidth == 0 — fill; > 0 — stroke (inward from the edge). No texture, color from rect.color.
    void emitRoundedSDF(const glm::mat4 &transform, const RectData &rect, float cornerRadius, float borderWidth);
};

} // namespace PandaUI
