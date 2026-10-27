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

    // Radii that fit the sides: adjacent corners may not overlap (the CSS rule — every radius scales
    // by the same factor when a side is too short).
    CornerRadii fitCornerRadii(Size size, CornerRadii radii) {
        radii.topLeft = std::max(0.f, radii.topLeft);
        radii.topRight = std::max(0.f, radii.topRight);
        radii.bottomRight = std::max(0.f, radii.bottomRight);
        radii.bottomLeft = std::max(0.f, radii.bottomLeft);
        float factor = 1.f;
        const auto limit = [&factor](float side, float a, float b) {
            if (a + b > side && a + b > 0.f) { factor = std::min(factor, side / (a + b)); }
        };
        limit(size.width, radii.topLeft, radii.topRight);
        limit(size.width, radii.bottomLeft, radii.bottomRight);
        limit(size.height, radii.topLeft, radii.bottomLeft);
        limit(size.height, radii.topRight, radii.bottomRight);
        if (factor < 1.f) {
            radii.topLeft *= factor;
            radii.topRight *= factor;
            radii.bottomRight *= factor;
            radii.bottomLeft *= factor;
        }
        return radii;
    }

    // A quarter arc per corner, `segments` straight pieces each, clockwise from the top right.
    // The counts are the same for every corner so an inner ring (a border) lines up with the outer.
    RoundedRectPerimeter buildRoundedRectPerimeter(Size size, CornerRadii radii, float inset, int segments) {
        RoundedRectPerimeter perimeter;
        if (size.width <= 0.f || size.height <= 0.f) { return perimeter; }

        const float maxInset = std::min(size.width, size.height) * 0.5f;
        inset = std::clamp(inset, 0.f, maxInset);
        const Size inner(std::max(0.f, size.width - inset * 2.f), std::max(0.f, size.height - inset * 2.f));
        // The inner edge of a border runs parallel to the outer one: its radii are smaller by the inset.
        radii = fitCornerRadii(
            inner, CornerRadii(
                       std::max(0.f, radii.topLeft - inset), std::max(0.f, radii.topRight - inset),
                       std::max(0.f, radii.bottomRight - inset), std::max(0.f, radii.bottomLeft - inset)
                   )
        );

        const float left = -0.5f + inset / size.width;
        const float right = 0.5f - inset / size.width;
        const float top = -0.5f + inset / size.height;
        const float bottom = 0.5f - inset / size.height;

        if (radii.isZero() && segments <= 1) {
            perimeter.push({left, top});
            perimeter.push({right, top});
            perimeter.push({right, bottom});
            perimeter.push({left, bottom});
            return perimeter;
        }

        const float pi = 3.14159265358979323846f;
        const float halfPi = pi * 0.5f;
        segments = std::max(1, segments);

        auto addCorner = [&](float cornerX, float cornerY, float radius, float startAngle, float endAngle) {
            // The arc center sits one radius inside the corner along both axes.
            const float radiusX = radius / size.width;
            const float radiusY = radius / size.height;
            const float centerX = cornerX + (cornerX < 0.f ? radiusX : -radiusX);
            const float centerY = cornerY + (cornerY < 0.f ? radiusY : -radiusY);
            for (int i = 0; i <= segments; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(segments);
                const float angle = startAngle + (endAngle - startAngle) * t;
                perimeter.push({centerX + std::cos(angle) * radiusX, centerY + std::sin(angle) * radiusY});
            }
        };

        addCorner(right, top, radii.topRight, -halfPi, 0.f);
        addCorner(right, bottom, radii.bottomRight, 0.f, halfPi);
        addCorner(left, bottom, radii.bottomLeft, halfPi, pi);
        addCorner(left, top, radii.topLeft, pi, pi + halfPi);
        return perimeter;
    }

    // The color of a point of the shape: the four corner colors, bilinearly.
    glm::vec4 colorAtLocalPoint(const UIRenderer::CornerColors &colors, glm::vec2 point) {
        const float u = std::clamp(point.x + 0.5f, 0.f, 1.f);
        const float v = std::clamp(point.y + 0.5f, 0.f, 1.f);
        const glm::vec4 top = glm::mix(glm::vec4(colors.topLeft), glm::vec4(colors.topRight), u);
        const glm::vec4 bottom = glm::mix(glm::vec4(colors.bottomLeft), glm::vec4(colors.bottomRight), u);
        return glm::mix(top, bottom, v);
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
    , m_currentHasMask(false)
    , m_currentMaskRect()
    , m_currentMaskRadii()
    , m_currentCommandHasMask(false)
    , m_currentCommandMaskRect()
    , m_currentCommandMaskRadii()
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
    m_currentHasMask = false;
    m_currentMaskRect = Rect::zero();
    m_currentMaskRadii = CornerRadii();
    reset();
    updateSolidTexture();
}

void UIRenderer::drawRect(const RectData &rect) {
    if (m_currentClipIsEmpty) { return; }
    drawRect(transformForRect(rect), rect);
}

void UIRenderer::drawRoundedRect(const RectData &rect, float cornerRadius) {
    drawRoundedRect(rect, CornerRadii(cornerRadius));
}

void UIRenderer::drawRoundedRect(const RectData &rect, CornerRadii radii) {
    if (m_currentClipIsEmpty) { return; }
    drawRoundedRect(transformForRect(rect), rect, radii, nullptr);
}

void UIRenderer::drawRoundedRect(const RectData &rect, CornerRadii radii, const CornerColors &colors) {
    if (m_currentClipIsEmpty) { return; }
    drawRoundedRect(transformForRect(rect), rect, radii, &colors);
}

void UIRenderer::drawRoundedRectBorder(const RectData &rect, float cornerRadius, float borderWidth) {
    drawRoundedRectBorder(rect, CornerRadii(cornerRadius), borderWidth);
}

void UIRenderer::drawRoundedRectBorder(const RectData &rect, CornerRadii radii, float borderWidth) {
    if (m_currentClipIsEmpty) { return; }
    drawRoundedRectBorder(transformForRect(rect), rect, radii, borderWidth);
}

void UIRenderer::drawShadow(const RectData &rect, float cornerRadius, ShadowData shadow) {
    drawShadow(rect, CornerRadii(cornerRadius), shadow);
}

void UIRenderer::drawNineSlice(const RectData &rect, EdgeInsets capInsets, float capScale) {
    if (m_currentClipIsEmpty || !rect.texture) { return; }
    if (rect.size.width <= 0.f || rect.size.height <= 0.f) { return; }
    const Size textureSize = rect.texture.size;
    const Rect &uv = rect.textureCoords;
    // The image in texture pixels (a part of an atlas when textureCoords is a sub-rectangle).
    const float imageWidth = textureSize.width * uv.size.width;
    const float imageHeight = textureSize.height * uv.size.height;
    const bool stretched = (capInsets.left <= 0.f && capInsets.right <= 0.f && capInsets.top <= 0.f &&
                            capInsets.bottom <= 0.f) ||
                           imageWidth <= 0.f || imageHeight <= 0.f;
    if (stretched) {
        drawRect(rect);
        return;
    }
    capScale = capScale > 0.f ? capScale : 1.f;
    // The caps in points; a destination smaller than two caps shrinks them in proportion.
    float capLeft = capInsets.left / capScale;
    float capRight = capInsets.right / capScale;
    float capTop = capInsets.top / capScale;
    float capBottom = capInsets.bottom / capScale;
    if (capLeft + capRight > rect.size.width && capLeft + capRight > 0.f) {
        const float factor = rect.size.width / (capLeft + capRight);
        capLeft *= factor;
        capRight *= factor;
    }
    if (capTop + capBottom > rect.size.height && capTop + capBottom > 0.f) {
        const float factor = rect.size.height / (capTop + capBottom);
        capTop *= factor;
        capBottom *= factor;
    }
    const float left = rect.center.x - rect.size.width * 0.5f;
    const float top = rect.center.y - rect.size.height * 0.5f;
    // Destination columns and rows (points) and the matching source columns and rows (image pixels).
    const float xs[4] = {0.f, capLeft, rect.size.width - capRight, rect.size.width};
    const float ys[4] = {0.f, capTop, rect.size.height - capBottom, rect.size.height};
    const float sx[4] = {0.f, std::min(capInsets.left, imageWidth), std::max(0.f, imageWidth - capInsets.right), imageWidth};
    const float sy[4] = {0.f, std::min(capInsets.top, imageHeight), std::max(0.f, imageHeight - capInsets.bottom), imageHeight};

    PrimBuffer buffer = beginPrimitive(rect.texture);
    if (!buffer) { return; }
    const glm::vec4 color = rect.color;
    const glm::mat4 model = rect.transform;
    // Image pixels → texture coordinates, in the convention of drawRect (v grows upwards).
    const auto texU = [&](float px) { return uv.origin.x + uv.size.width * (px / imageWidth); };
    const auto texV = [&](float py) { return uv.origin.y + uv.size.height * (1.f - py / imageHeight); };
    uint32_t vertexCount = 0;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            const float x0 = left + xs[column];
            const float x1 = left + xs[column + 1];
            const float y0 = top + ys[row];
            const float y1 = top + ys[row + 1];
            if (x1 <= x0 || y1 <= y0) { continue; }
            const float u0 = texU(sx[column]);
            const float u1 = texU(sx[column + 1]);
            const float v0 = texV(sy[row]);
            const float v1 = texV(sy[row + 1]);
            buffer.vertex(glm::vec3(model * glm::vec4(x0, y0, 0.f, 1.f)), {u0, v0}, color);
            buffer.vertex(glm::vec3(model * glm::vec4(x1, y0, 0.f, 1.f)), {u1, v0}, color);
            buffer.vertex(glm::vec3(model * glm::vec4(x1, y1, 0.f, 1.f)), {u1, v1}, color);
            buffer.vertex(glm::vec3(model * glm::vec4(x0, y1, 0.f, 1.f)), {u0, v1}, color);
            buffer.quad(vertexCount, vertexCount + 1, vertexCount + 2, vertexCount + 3);
            vertexCount += 4;
        }
    }
}

void UIRenderer::drawShadow(const RectData &rect, CornerRadii radii, ShadowData shadow) {
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
        drawRoundedRect(
            shadowRect, CornerRadii(
                            radii.topLeft + spread, radii.topRight + spread, radii.bottomRight + spread,
                            radii.bottomLeft + spread
                        )
        );
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

void UIRenderer::emitRoundedSDF(
    const glm::mat4 &transform, const RectData &rect, float cornerRadius, float borderWidth, const CornerColors *colors
) {
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
    // A gradient gives every corner its own color; the shader interpolates them across the quad.
    auto corner = [&](float sx, float sy, Color cornerColor) {
        buffer.vertex(
            glm::vec3(model * glm::vec4(sx * nx, sy * ny, 0.f, 1.f)),
            glm::vec2(sx * nx * rect.size.width, sy * ny * rect.size.height),
            colors ? glm::vec4(cornerColor) : color, params
        );
    };
    corner(-1.f, -1.f, colors ? colors->topLeft : rect.color);    // TL
    corner(1.f, -1.f, colors ? colors->topRight : rect.color);    // TR
    corner(1.f, 1.f, colors ? colors->bottomRight : rect.color);  // BR
    corner(-1.f, 1.f, colors ? colors->bottomLeft : rect.color);  // BL
    buffer.quad(0, 1, 2, 3);
}

void UIRenderer::drawRoundedRect(const glm::mat4 &transform, RectData rect, CornerRadii radii, const CornerColors *colors) {
    if (rect.size.width <= 0.f || rect.size.height <= 0.f) { return; }
    radii = fitCornerRadii(rect.size, radii);
    if (radii.isZero() && !colors) {
        drawRect(transform, rect);
        return;
    }

    // A solid or gradient fill with one radius — the analytic SDF (perfect AA). Differing radii or
    // a texture — the fan tessellation.
    if (!rect.texture && radii.isUniform()) {
        emitRoundedSDF(transform, rect, radii.topLeft, 0.f, colors);
        return;
    }

    resolveSolidTexture(rect);
    const int segments = segmentsForRadius(radii.max());
    const RoundedRectPerimeter perimeter = buildRoundedRectPerimeter(rect.size, radii, 0.f, segments);
    if (perimeter.count < 3) { return; }

    PrimBuffer buffer = beginPrimitive(rect.texture);
    if (!buffer) { return; }

    const glm::vec4 color = rect.color;
    const glm::mat4 model = rect.transform * transform;
    const auto colorAt = [&](glm::vec2 point) { return colors ? colorAtLocalPoint(*colors, point) : color; };

    const uint32_t center = buffer.vertex(
        glm::vec3(model * glm::vec4(0.f, 0.f, 0.f, 1.f)), textureCoordsForLocalPoint(rect.textureCoords, {0.f, 0.f}),
        colorAt({0.f, 0.f})
    );
    for (uint32_t i = 0; i < perimeter.count; ++i) {
        const glm::vec2 point = perimeter.points[i];
        buffer.vertex(
            glm::vec3(model * glm::vec4(point.x, point.y, 0.f, 1.f)), textureCoordsForLocalPoint(rect.textureCoords, point),
            colorAt(point)
        );
    }
    buffer.fan(center, center + 1, perimeter.count);
}

void UIRenderer::drawRoundedRectBorder(const glm::mat4 &transform, RectData rect, CornerRadii radii, float borderWidth) {
    if (borderWidth <= 0.f || rect.size.width <= 0.f || rect.size.height <= 0.f) { return; }

    const float maxBorderWidth = std::min(rect.size.width, rect.size.height) * 0.5f;
    borderWidth = std::min(borderWidth, maxBorderWidth);
    radii = fitCornerRadii(rect.size, radii);

    // One radius: fill and stroke from a single distance, perfect AA (sharp corners included).
    if (radii.isUniform()) {
        emitRoundedSDF(transform, rect, radii.topLeft, borderWidth, nullptr);
        return;
    }

    // Differing radii: a ring between the outer perimeter and the one inset by the border width.
    resolveSolidTexture(rect);
    const int segments = segmentsForRadius(radii.max());
    const RoundedRectPerimeter outer = buildRoundedRectPerimeter(rect.size, radii, 0.f, segments);
    const RoundedRectPerimeter inner = buildRoundedRectPerimeter(rect.size, radii, borderWidth, segments);
    if (outer.count < 3 || outer.count != inner.count) { return; }

    PrimBuffer buffer = beginPrimitive(rect.texture);
    if (!buffer) { return; }

    const glm::vec4 color = rect.color;
    const glm::mat4 model = rect.transform * transform;
    const glm::vec2 uv = textureCoordsForLocalPoint(rect.textureCoords, {0.f, 0.f});
    for (uint32_t i = 0; i < outer.count; ++i) {
        const glm::vec2 point = outer.points[i];
        buffer.vertex(glm::vec3(model * glm::vec4(point.x, point.y, 0.f, 1.f)), uv, color);
    }
    for (uint32_t i = 0; i < inner.count; ++i) {
        const glm::vec2 point = inner.points[i];
        buffer.vertex(glm::vec3(model * glm::vec4(point.x, point.y, 0.f, 1.f)), uv, color);
    }
    buffer.ring(0, outer.count, outer.count);
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
    // The clip does not change within a single drawText; the texture is the face's, and only a glyph
    // taken from the fallback chain (a character the face lacks) switches to that atlas and back.
    syncCurrentBatchState(texture, DrawPrimitive::Textured);
    TextureHandle boundTexture = texture;
    const glm::vec4 vertexColor = color;

    for (size_t i = 0; i < text.size();) {
        const uint32_t codepoint = decodeUtf8(text, i);
        if (codepoint == '\n') {
            penX = startX;
            baselineY += font.getLineHeight() * scale;
            continue;
        }

        const FontAtlas::GlyphRef ref = font.resolveGlyph(codepoint);
        if (!ref.glyph || !ref.atlas || ref.atlas->getPixelSize() <= 0.f) { continue; }
        const FontAtlas::Glyph *glyph = ref.glyph;
        const float glyphScale = fontSize / ref.atlas->getPixelSize();
        const TextureHandle glyphTexture = ref.atlas->getTexture();
        if (glyphTexture != boundTexture) {
            syncCurrentBatchState(glyphTexture, DrawPrimitive::Textured);
            boundTexture = glyphTexture;
        }

        const float x0 = penX + glyph->x0 * glyphScale;
        const float y0 = baselineY + glyph->y0 * glyphScale;
        const float x1 = penX + glyph->x1 * glyphScale;
        const float y1 = baselineY + glyph->y1 * glyphScale;
        penX += glyph->advanceX * glyphScale + letterSpacing; // tracking between glyphs (uppercase headers)

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
    ClipState state;
    state.rect = rect;
    pushClip(state);
}

void UIRenderer::pushClipRoundedRect(Rect rect, float cornerRadius) {
    pushClipRoundedRect(rect, CornerRadii(cornerRadius));
}

void UIRenderer::pushClipRoundedRect(Rect rect, CornerRadii radii) {
    ClipState state;
    state.rect = rect;
    radii = fitCornerRadii(rect.size, radii);
    if (!radii.isZero()) {
        state.hasMask = true;
        state.maskRect = rect;
        state.maskRadii = radii;
    }
    pushClip(state);
}

void UIRenderer::pushClip(const ClipState &state) {
    flush();
    ClipState next = state;
    if (m_hasCurrentClip) {
        next.rect = intersectRects(m_currentClipRect, state.rect);
        // The innermost rounded clip is the mask; a plain clip inside a rounded one keeps the outer mask.
        if (!state.hasMask && m_currentHasMask) {
            next.hasMask = true;
            next.maskRect = m_currentMaskRect;
            next.maskRadii = m_currentMaskRadii;
        }
    }
    m_clipStack.push_back(next);
    applyClipState();
}

void UIRenderer::popClipRect() {
    if (m_clipStack.empty()) { return; }
    flush();
    m_clipStack.pop_back();
    applyClipState();
}

void UIRenderer::applyClipState() {
    m_hasCurrentClip = !m_clipStack.empty();
    if (m_hasCurrentClip) {
        const ClipState &state = m_clipStack.back();
        m_currentClipRect = state.rect;
        m_currentClipIsEmpty = !hasArea(state.rect);
        m_currentHasMask = state.hasMask;
        m_currentMaskRect = state.maskRect;
        m_currentMaskRadii = state.maskRadii;
    } else {
        m_currentClipRect = Rect::zero();
        m_currentClipIsEmpty = false;
        m_currentHasMask = false;
        m_currentMaskRect = Rect::zero();
        m_currentMaskRadii = CornerRadii();
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
    m_currentCommandHasMask = false;
    m_currentCommandMaskRect = Rect::zero();
    m_currentCommandMaskRadii = CornerRadii();
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
    const bool hasMask = hasClip && m_currentHasMask;
    const Rect maskRect = hasMask ? m_currentMaskRect : Rect::zero();
    const CornerRadii maskRadii = hasMask ? m_currentMaskRadii : CornerRadii();
    const auto sameRect = [](const Rect &a, const Rect &b) {
        return a.origin.x == b.origin.x && a.origin.y == b.origin.y && a.size.width == b.size.width &&
               a.size.height == b.size.height;
    };
    if (m_currentCommandIndexCount > 0 &&
        (texture != m_currentTexture || primitive != m_currentPrimitive || hasClip != m_currentCommandHasClip ||
         !sameRect(clipRect, m_currentCommandClipRect) || hasMask != m_currentCommandHasMask ||
         !sameRect(maskRect, m_currentCommandMaskRect) || maskRadii != m_currentCommandMaskRadii)) {
        flush();
    }

    if (m_currentCommandIndexCount == 0) {
        m_currentTexture = texture;
        m_currentPrimitive = primitive;
        m_currentCommandIndexOffset = static_cast<uint32_t>(m_drawList.indices.size());
        m_currentCommandHasClip = hasClip;
        m_currentCommandClipRect = clipRect;
        m_currentCommandHasMask = hasMask;
        m_currentCommandMaskRect = maskRect;
        m_currentCommandMaskRadii = maskRadii;
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
    if (m_currentCommandHasMask) {
        const Rect &mask = m_currentCommandMaskRect;
        const CornerRadii &radii = m_currentCommandMaskRadii;
        command.mask.rect = glm::vec4(mask.origin.x, mask.origin.y, mask.size.width, mask.size.height);
        command.mask.radii = glm::vec4(radii.topLeft, radii.topRight, radii.bottomRight, radii.bottomLeft);
        command.mask.params = glm::vec4(1.f, 0.f, 0.f, 0.f);
    }
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
