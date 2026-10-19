#include "PandaUI/Rendering/UIRenderer.hpp"

#include "PandaUI/Runtime/Context.hpp"
#include "PandaUI/Core/Profile.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>

namespace PandaUI {

namespace {

    constexpr int MAX_CORNER_SEGMENTS = 16;
    constexpr int MAX_ROUNDED_RECT_POINTS = (MAX_CORNER_SEGMENTS + 1) * 4;

    struct RoundedRectPerimeter {
        std::array<glm::vec2, MAX_ROUNDED_RECT_POINTS> points;
        uint32_t count = 0;

        void push(glm::vec2 point) {
            assert(count < points.size() && "ROUNDED RECT PERIMETER LIMIT REACHED");
            points[count++] = point;
        }
    };

    uint32_t decodeUtf8(std::string_view text, size_t &index) {
        const auto *bytes = reinterpret_cast<const unsigned char *>(text.data());
        const unsigned char first = bytes[index++];
        if (first < 0x80) { return first; }

        if ((first >> 5) == 0x6 && index < text.size()) {
            const uint32_t second = bytes[index++] & 0x3F;
            return ((first & 0x1F) << 6) | second;
        }

        if ((first >> 4) == 0xE && index + 1 < text.size()) {
            const uint32_t second = bytes[index++] & 0x3F;
            const uint32_t third = bytes[index++] & 0x3F;
            return ((first & 0x0F) << 12) | (second << 6) | third;
        }

        if ((first >> 3) == 0x1E && index + 2 < text.size()) {
            const uint32_t second = bytes[index++] & 0x3F;
            const uint32_t third = bytes[index++] & 0x3F;
            const uint32_t fourth = bytes[index++] & 0x3F;
            return ((first & 0x07) << 18) | (second << 12) | (third << 6) | fourth;
        }

        return '?';
    }

    bool hasArea(const Rect &rect) {
        return rect.size.width > 0.f && rect.size.height > 0.f;
    }

    Rect intersectRects(const Rect &lhs, const Rect &rhs) {
        const float x0 = std::max(lhs.origin.x, rhs.origin.x);
        const float y0 = std::max(lhs.origin.y, rhs.origin.y);
        const float x1 = std::min(lhs.origin.x + lhs.size.width, rhs.origin.x + rhs.size.width);
        const float y1 = std::min(lhs.origin.y + lhs.size.height, rhs.origin.y + rhs.size.height);

        Rect result;
        result.origin = Point(x0, y0);
        result.size = Size(std::max(0.f, x1 - x0), std::max(0.f, y1 - y0));
        return result;
    }

    float clampCornerRadius(Size size, float cornerRadius) {
        return std::clamp(cornerRadius, 0.f, std::min(size.width, size.height) * 0.5f);
    }

    int segmentsForRadius(float radius) {
        if (radius <= 0.f) { return 1; }
        return std::clamp(static_cast<int>(std::ceil(radius / 4.f)), 4, MAX_CORNER_SEGMENTS);
    }

    glm::vec2 textureCoordsForLocalPoint(Rect textureCoords, glm::vec2 point) {
        return {
            textureCoords.origin.x + (point.x + 0.5f) * textureCoords.size.width,
            textureCoords.origin.y + (0.5f - point.y) * textureCoords.size.height,
        };
    }

    glm::mat4 transformForRect(const UIRenderer::RectData &rect) {
        const glm::mat4 identity(1.f);
        return glm::translate(identity, glm::vec3(rect.center.x, rect.center.y, rect.center.z)) *
               glm::rotate(identity, glm::radians(rect.rotation), glm::vec3(0.f, 0.f, 1.f)) *
               glm::scale(identity, glm::vec3(rect.size.width, rect.size.height, 1.f));
    }

    RoundedRectPerimeter buildRoundedRectPerimeter(Size size, float cornerRadius, float inset, int segments) {
        RoundedRectPerimeter perimeter;
        if (size.width <= 0.f || size.height <= 0.f) { return perimeter; }

        const float maxInset = std::min(size.width, size.height) * 0.5f;
        inset = std::clamp(inset, 0.f, maxInset);
        const float innerWidth = std::max(0.f, size.width - inset * 2.f);
        const float innerHeight = std::max(0.f, size.height - inset * 2.f);
        cornerRadius = std::clamp(cornerRadius, 0.f, std::min(innerWidth, innerHeight) * 0.5f);

        const float left = -0.5f + inset / size.width;
        const float right = 0.5f - inset / size.width;
        const float top = -0.5f + inset / size.height;
        const float bottom = 0.5f - inset / size.height;

        if (cornerRadius <= 0.f && segments <= 1) {
            perimeter.push({left, top});
            perimeter.push({right, top});
            perimeter.push({right, bottom});
            perimeter.push({left, bottom});
            return perimeter;
        }

        const float radiusX = cornerRadius / size.width;
        const float radiusY = cornerRadius / size.height;
        const float pi = 3.14159265358979323846f;
        const float halfPi = pi * 0.5f;

        auto addCorner = [&](float centerX, float centerY, float startAngle, float endAngle) {
            for (int i = 0; i <= segments; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(segments);
                const float angle = startAngle + (endAngle - startAngle) * t;
                perimeter.push({centerX + std::cos(angle) * radiusX, centerY + std::sin(angle) * radiusY});
            }
        };

        addCorner(right - radiusX, top + radiusY, -halfPi, 0.f);
        addCorner(right - radiusX, bottom - radiusY, 0.f, halfPi);
        addCorner(left + radiusX, bottom - radiusY, halfPi, pi);
        addCorner(left + radiusX, top + radiusY, pi, pi + halfPi);
        return perimeter;
    }

} // namespace

UIRenderer::UIRenderer()
    : m_viewProj(1.f)
    , m_drawList()
    , m_stats()
    , m_currentTexture()
    , m_currentPrimitive(DrawPrimitive::Textured)
    , m_currentCommandIndexOffset(0)
    , m_currentCommandIndexCount(0)
    , m_currentCommandHasClip(false)
    , m_currentCommandClipRect()
    , m_viewportSize()
    , m_dpiScale(1.f, 1.f)
    , m_clipStack()
    , m_currentClipRect()
    , m_hasCurrentClip(false)
    , m_currentClipIsEmpty(false)
    , m_solidTexture()
    , m_solidTextureCoords(0, 0, 1, 1) {
    m_drawList.vertices.reserve(4096);
    m_drawList.indices.reserve(6144);
    m_drawList.commands.reserve(64);
}

UIRenderer::~UIRenderer() = default;

void UIRenderer::begin() {
    PANDA_UI_PROFILE_SCOPE_N("PandaUI::UIRenderer::begin");
    m_clipStack.clear();
    m_hasCurrentClip = false;
    m_currentClipIsEmpty = false;
    reset();
    updateSolidTexture();
}

void UIRenderer::drawRect(const RectData &rect) {
    if (m_currentClipIsEmpty) { return; }
    drawRect(transformForRect(rect), rect);
}

void UIRenderer::drawRoundedRect(const RectData &rect, float cornerRadius) {
    if (m_currentClipIsEmpty) { return; }
    drawRoundedRect(transformForRect(rect), rect, cornerRadius);
}

void UIRenderer::drawRoundedRectBorder(const RectData &rect, float cornerRadius, float borderWidth) {
    if (m_currentClipIsEmpty) { return; }
    drawRoundedRectBorder(transformForRect(rect), rect, cornerRadius, borderWidth);
}

void UIRenderer::drawShadow(const RectData &rect, float cornerRadius, ShadowData shadow) {
    if (m_currentClipIsEmpty || shadow.color.a <= 0.f) { return; }

    const int layerCount = std::max(1, std::min(10, static_cast<int>(std::ceil(std::max(1.f, shadow.radius) / 3.f))));
    for (int layer = layerCount; layer >= 1; --layer) {
        const float t = static_cast<float>(layer) / static_cast<float>(layerCount);
        const float spread = shadow.spread + shadow.radius * t;
        RectData shadowRect = rect;
        shadowRect.center.x += shadow.offset.x;
        shadowRect.center.y += shadow.offset.y;
        shadowRect.size.width += spread * 2.f;
        shadowRect.size.height += spread * 2.f;
        shadowRect.color = shadow.color;
        shadowRect.color.a *= (1.f - t * 0.65f) / static_cast<float>(layerCount);
        drawRoundedRect(shadowRect, cornerRadius + spread);
    }
}

void UIRenderer::drawRect(const glm::mat4 &transform, RectData rect) {
    resolveSolidTexture(rect);
    PrimBuffer buffer = beginPrimitive(rect.texture);
    if (!buffer) { return; }

    const glm::vec4 color = rect.color;
    const glm::mat4 model = rect.transform * transform;
    const Rect &uv = rect.textureCoords;
    const float uvL = uv.origin.x;
    const float uvR = uv.origin.x + uv.size.width;
    const float uvT = uv.origin.y;
    const float uvB = uv.origin.y + uv.size.height;

    // Vertices clockwise: TL, TR, BR, BL.
    buffer.vertex(glm::vec3(model * glm::vec4(-0.5f, -0.5f, 0.f, 1.f)), {uvL, uvB}, color);
    buffer.vertex(glm::vec3(model * glm::vec4(0.5f, -0.5f, 0.f, 1.f)), {uvR, uvB}, color);
    buffer.vertex(glm::vec3(model * glm::vec4(0.5f, 0.5f, 0.f, 1.f)), {uvR, uvT}, color);
    buffer.vertex(glm::vec3(model * glm::vec4(-0.5f, 0.5f, 0.f, 1.f)), {uvL, uvT}, color);
    buffer.quad(0, 1, 2, 3);
}

void UIRenderer::emitRoundedSDF(const glm::mat4 &transform, const RectData &rect, float cornerRadius, float borderWidth) {
    if (rect.size.width <= 0.f || rect.size.height <= 0.f) { return; }

    PrimBuffer buffer = beginPrimitive(TextureHandle{}, DrawPrimitive::RoundedSDF);
    if (!buffer) { return; }

    // The quad is expanded outwards by a couple of pixels: otherwise the AA fringe at the very edge is
    // cut off by the quad boundary. Outside the shape the SDF yields alpha = 0, so the margin is invisible.
    constexpr float AA_PAD = 2.f;
    const float nx = 0.5f + AA_PAD / rect.size.width;
    const float ny = 0.5f + AA_PAD / rect.size.height;
    const float halfW = rect.size.width * 0.5f;
    const float halfH = rect.size.height * 0.5f;

    const glm::vec4 color = rect.color;
    const glm::mat4 model = rect.transform * transform;
    // xy — half-size of the shape in pixels, z — radius, w — stroke width (0 = fill).
    const glm::vec4 params(halfW, halfH, cornerRadius, borderWidth);

    // localPos (through the uv slot) — the local position of the corner relative to the center, in pixels.
    auto corner = [&](float sx, float sy) {
        buffer.vertex(
            glm::vec3(model * glm::vec4(sx * nx, sy * ny, 0.f, 1.f)),
            glm::vec2(sx * nx * rect.size.width, sy * ny * rect.size.height), color, params
        );
    };
    corner(-1.f, -1.f); // TL
    corner(1.f, -1.f);  // TR
    corner(1.f, 1.f);   // BR
    corner(-1.f, 1.f);  // BL
    buffer.quad(0, 1, 2, 3);
}

void UIRenderer::drawRoundedRect(const glm::mat4 &transform, RectData rect, float cornerRadius) {
    cornerRadius = clampCornerRadius(rect.size, cornerRadius);
    if (cornerRadius <= 0.f) {
        drawRect(transform, rect);
        return;
    }
    if (rect.size.width <= 0.f || rect.size.height <= 0.f) { return; }

    // Solid fill — analytic SDF (perfect AA). A rounded rectangle with
    // a texture (a rare case) — the previous fan tessellation.
    if (!rect.texture) {
        emitRoundedSDF(transform, rect, cornerRadius, 0.f);
        return;
    }

    const int segments = segmentsForRadius(cornerRadius);
    const RoundedRectPerimeter perimeter = buildRoundedRectPerimeter(rect.size, cornerRadius, 0.f, segments);
    if (perimeter.count < 3) { return; }

    PrimBuffer buffer = beginPrimitive(rect.texture);
    if (!buffer) { return; }

    const glm::vec4 color = rect.color;
    const glm::mat4 model = rect.transform * transform;

    const uint32_t center = buffer.vertex(
        glm::vec3(model * glm::vec4(0.f, 0.f, 0.f, 1.f)), textureCoordsForLocalPoint(rect.textureCoords, {0.f, 0.f}),
        color
    );
    for (uint32_t i = 0; i < perimeter.count; ++i) {
        const glm::vec2 point = perimeter.points[i];
        buffer.vertex(
            glm::vec3(model * glm::vec4(point.x, point.y, 0.f, 1.f)), textureCoordsForLocalPoint(rect.textureCoords, point),
            color
        );
    }
    buffer.fan(center, center + 1, perimeter.count);
}

void UIRenderer::drawRoundedRectBorder(
    const glm::mat4 &transform, RectData rect, float cornerRadius, float borderWidth
) {
    if (borderWidth <= 0.f || rect.size.width <= 0.f || rect.size.height <= 0.f) { return; }

    const float maxBorderWidth = std::min(rect.size.width, rect.size.height) * 0.5f;
    borderWidth = std::min(borderWidth, maxBorderWidth);
    cornerRadius = clampCornerRadius(rect.size, cornerRadius);

    // The stroke is always a solid color — a single SDF path (including sharp corners at radius 0):
    // fill and stroke from a single distance, perfect AA.
    emitRoundedSDF(transform, rect, cornerRadius, borderWidth);
}

void UIRenderer::drawText(
    const FontAtlas &font, std::string_view text, Point position, Color color, float fontSize,
    float letterSpacing, bool italic
) {
    if (m_currentClipIsEmpty) { return; }
    const TextureHandle texture = font.getTexture();
    if (!texture) { return; }
    if (fontSize <= 0.f) { fontSize = font.getPixelSize(); }

    const float scale = fontSize / font.getPixelSize();
    const float startX = position.x;
    float penX = startX;
    float baselineY = position.y + font.getAscent() * scale;

    // Glyphs are axis-aligned in screen coordinates: the corners are computed directly, without matrices.
    // The texture and clip do not change within a single drawText, so the batch state is
    // synchronized once, and every glyph is written into the already open batch.
    syncCurrentBatchState(texture, DrawPrimitive::Textured);
    const glm::vec4 vertexColor = color;

    for (size_t i = 0; i < text.size();) {
        const uint32_t codepoint = decodeUtf8(text, i);
        if (codepoint == '\n') {
            penX = startX;
            baselineY += font.getLineHeight() * scale;
            continue;
        }

        const FontAtlas::Glyph *glyph = font.findGlyph(codepoint);
        if (!glyph) { continue; }

        const float x0 = penX + glyph->x0 * scale;
        const float y0 = baselineY + glyph->y0 * scale;
        const float x1 = penX + glyph->x1 * scale;
        const float y1 = baselineY + glyph->y1 * scale;
        penX += glyph->advanceX * scale + letterSpacing; // tracking between glyphs (uppercase headers)

        if (x1 <= x0 || y1 <= y0) { continue; }

        // Synthetic italic: the X shift is proportional to the height above the baseline (~11°) —
        // the glyph base stays in place, the top moves to the right, descenders (p, g) — slightly to the left.
        constexpr float ITALIC_SLANT = 0.2f;
        const float skew0 = italic ? (baselineY - y0) * ITALIC_SLANT : 0.f; // top vertices
        const float skew1 = italic ? (baselineY - y1) * ITALIC_SLANT : 0.f; // bottom vertices

        PrimBuffer prim = continuePrimitive();
        prim.vertex(glm::vec2(x0 + skew0, y0), glm::vec2(glyph->u0, glyph->v0), vertexColor);
        prim.vertex(glm::vec2(x1 + skew0, y0), glm::vec2(glyph->u1, glyph->v0), vertexColor);
        prim.vertex(glm::vec2(x1 + skew1, y1), glm::vec2(glyph->u1, glyph->v1), vertexColor);
        prim.vertex(glm::vec2(x0 + skew1, y1), glm::vec2(glyph->u0, glyph->v1), vertexColor);
        prim.quad(0, 1, 2, 3);
    }
}

void UIRenderer::drawGradientRect(Rect rect, Color topLeft, Color topRight, Color bottomRight, Color bottomLeft) {
    if (!m_solidTexture) { return; }
    if (rect.size.width <= 0.f || rect.size.height <= 0.f) { return; }

    PrimBuffer buffer = beginPrimitive(m_solidTexture);
    if (!buffer) { return; }

    const glm::vec2 uv(m_solidTextureCoords.origin.x, m_solidTextureCoords.origin.y);
    const float x0 = rect.origin.x;
    const float y0 = rect.origin.y;
    const float x1 = x0 + rect.size.width;
    const float y1 = y0 + rect.size.height;

    buffer.vertex(glm::vec2(x0, y0), uv, glm::vec4(topLeft));
    buffer.vertex(glm::vec2(x1, y0), uv, glm::vec4(topRight));
    buffer.vertex(glm::vec2(x1, y1), uv, glm::vec4(bottomRight));
    buffer.vertex(glm::vec2(x0, y1), uv, glm::vec4(bottomLeft));
    buffer.quad(0, 1, 2, 3);
}

void UIRenderer::drawTriangle(Point a, Point b, Point c, Color color) {
    if (!m_solidTexture) { return; }

    PrimBuffer buffer = beginPrimitive(m_solidTexture);
    if (!buffer) { return; }

    const glm::vec2 uv(m_solidTextureCoords.origin.x, m_solidTextureCoords.origin.y);
    const glm::vec4 rgba(color);

    buffer.vertex(glm::vec2(a.x, a.y), uv, rgba);
    buffer.vertex(glm::vec2(b.x, b.y), uv, rgba);
    buffer.vertex(glm::vec2(c.x, c.y), uv, rgba);
    buffer.triangle(0, 1, 2);
}

void UIRenderer::fillCircle(Point center, float radius, Color color, int segments) {
    if (!m_solidTexture || radius <= 0.f) { return; }
    if (segments <= 0) { segments = std::clamp(static_cast<int>(std::ceil(radius * 2.f)), 6, 32); }

    PrimBuffer buffer = beginPrimitive(m_solidTexture);
    if (!buffer) { return; }

    const glm::vec2 uv(m_solidTextureCoords.origin.x, m_solidTextureCoords.origin.y);
    const glm::vec4 rgba(color);
    const float tau = 6.28318530718f;

    const uint32_t centerIndex = buffer.vertex(glm::vec2(center.x, center.y), uv, rgba);
    for (int i = 0; i < segments; ++i) {
        const float angle = tau * static_cast<float>(i) / static_cast<float>(segments);
        buffer.vertex(glm::vec2(center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius), uv, rgba);
    }
    buffer.fan(centerIndex, centerIndex + 1, static_cast<uint32_t>(segments));
}

void UIRenderer::drawLine(Point a, Point b, float thickness, Color color) {
    if (!m_solidTexture || thickness <= 0.f) { return; }

    const glm::vec2 pa(a.x, a.y);
    const glm::vec2 pb(b.x, b.y);
    const glm::vec2 delta = pb - pa;
    const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    const float radius = thickness * 0.5f;

    if (length > 1e-4f) {
        const glm::vec2 dir = delta / length;
        const glm::vec2 offset(-dir.y * radius, dir.x * radius); // normal * half-thickness
        if (PrimBuffer buffer = beginPrimitive(m_solidTexture)) {
            const glm::vec2 uv(m_solidTextureCoords.origin.x, m_solidTextureCoords.origin.y);
            const glm::vec4 rgba(color);
            buffer.vertex(pa - offset, uv, rgba);
            buffer.vertex(pb - offset, uv, rgba);
            buffer.vertex(pb + offset, uv, rgba);
            buffer.vertex(pa + offset, uv, rgba);
            buffer.quad(0, 1, 2, 3);
        }
    }

    // Round caps (they also smooth the joint between segments at the corners of a polyline).
    fillCircle(a, radius, color, 0);
    fillCircle(b, radius, color, 0);
}

UIRenderer::Statistics UIRenderer::getStats() {
    Statistics stats = m_stats;
    stats.vertexCount = static_cast<uint32_t>(m_drawList.vertices.size());
    stats.indexCount = static_cast<uint32_t>(m_drawList.indices.size());
    return stats;
}

void UIRenderer::end() {
    PANDA_UI_PROFILE_SCOPE_N("PandaUI::UIRenderer::end");
    flush();
    PANDA_UI_PROFILE_PLOT("PandaUI Core Draw Calls", static_cast<int64_t>(m_stats.drawCalls));
    PANDA_UI_PROFILE_PLOT("PandaUI Primitives", static_cast<int64_t>(m_stats.primitiveCount));
}

void UIRenderer::pushClipRect(Rect rect) {
    flush();
    Rect nextClip = rect;
    if (m_hasCurrentClip) { nextClip = intersectRects(m_currentClipRect, rect); }
    m_clipStack.push_back(nextClip);
    m_currentClipRect = nextClip;
    m_hasCurrentClip = true;
    m_currentClipIsEmpty = !hasArea(nextClip);
}

void UIRenderer::popClipRect() {
    if (m_clipStack.empty()) { return; }
    flush();
    m_clipStack.pop_back();
    m_hasCurrentClip = !m_clipStack.empty();
    if (m_hasCurrentClip) {
        m_currentClipRect = m_clipStack.back();
        m_currentClipIsEmpty = !hasArea(m_currentClipRect);
    } else {
        m_currentClipRect = Rect::zero();
        m_currentClipIsEmpty = false;
    }
}

void UIRenderer::reset() {
    m_drawList.clear();
    m_drawList.viewProjection = m_viewProj;
    m_drawList.viewportSize = m_viewportSize;
    m_drawList.dpiScale = m_dpiScale;
    m_stats = {};
    m_currentTexture = {};
    m_currentPrimitive = DrawPrimitive::Textured;
    m_currentCommandIndexOffset = 0;
    m_currentCommandIndexCount = 0;
    m_currentCommandHasClip = false;
    m_currentCommandClipRect = Rect::zero();
}

void UIRenderer::updateSolidTexture() {
    m_solidTexture = {};
    m_solidTextureCoords = Rect(0, 0, 1, 1);
    if (!Context::hasCurrent()) { return; }

    std::shared_ptr<FontAtlas> font = Context::current().getFontAtlas();
    if (!font || !font->getTexture()) { return; }

    m_solidTexture = font->getTexture();
    m_solidTextureCoords = font->getWhiteTextureCoords();
}

void UIRenderer::syncCurrentBatchState(TextureHandle texture, DrawPrimitive primitive) {
    const bool hasClip = m_hasCurrentClip && !m_currentClipIsEmpty;
    const Rect clipRect = hasClip ? m_currentClipRect : Rect::zero();
    if (m_currentCommandIndexCount > 0 && (texture != m_currentTexture || primitive != m_currentPrimitive ||
                                           hasClip != m_currentCommandHasClip ||
                                           clipRect.origin.x != m_currentCommandClipRect.origin.x ||
                                           clipRect.origin.y != m_currentCommandClipRect.origin.y ||
                                           clipRect.size.width != m_currentCommandClipRect.size.width ||
                                           clipRect.size.height != m_currentCommandClipRect.size.height)) {
        flush();
    }

    if (m_currentCommandIndexCount == 0) {
        m_currentTexture = texture;
        m_currentPrimitive = primitive;
        m_currentCommandIndexOffset = static_cast<uint32_t>(m_drawList.indices.size());
        m_currentCommandHasClip = hasClip;
        m_currentCommandClipRect = clipRect;
    }
}

PrimBuffer UIRenderer::beginPrimitive(TextureHandle texture, DrawPrimitive primitive) {
    if (m_currentClipIsEmpty) { return PrimBuffer(); }
    syncCurrentBatchState(texture, primitive);
    m_stats.primitiveCount += 1;
    return PrimBuffer(m_drawList, m_currentCommandIndexCount, static_cast<uint32_t>(m_drawList.vertices.size()));
}

PrimBuffer UIRenderer::continuePrimitive() {
    m_stats.primitiveCount += 1;
    return PrimBuffer(m_drawList, m_currentCommandIndexCount, static_cast<uint32_t>(m_drawList.vertices.size()));
}

void UIRenderer::resolveSolidTexture(RectData &rect) const {
    if (!rect.texture && m_solidTexture) {
        rect.texture = m_solidTexture;
        rect.textureCoords = m_solidTextureCoords;
    }
}

void UIRenderer::flush() {
    if (m_currentCommandIndexCount == 0) { return; }

    DrawCommand command;
    command.indexOffset = m_currentCommandIndexOffset;
    command.indexCount = m_currentCommandIndexCount;
    command.texture = m_currentTexture;
    command.hasClip = m_currentCommandHasClip;
    command.clipRect = m_currentCommandClipRect;
    command.primitive = m_currentPrimitive;
    m_drawList.commands.push_back(command);
    m_stats.drawCalls += 1;

    m_currentCommandIndexOffset = static_cast<uint32_t>(m_drawList.indices.size());
    m_currentCommandIndexCount = 0;
}

void UIRenderer::setViewportSize(Size viewportSize, Size dpiScale) {
    m_viewportSize = viewportSize;
    m_dpiScale = dpiScale;
    m_drawList.viewportSize = viewportSize;
    m_drawList.dpiScale = dpiScale;
}

void UIRenderer::setViewProj(glm::mat4 viewProj) {
    m_viewProj = viewProj;
    m_drawList.viewProjection = viewProj;
}

} // namespace PandaUI
