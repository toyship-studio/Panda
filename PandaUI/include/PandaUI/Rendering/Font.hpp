#pragma once

#include "PandaUI/Core/Base.hpp"
#include "PandaUI/Core/TextureHandle.hpp"
#include "PandaUI/Rendering/TextLayout.hpp" // FontFamily, FontWeight

#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace PandaUI {

// Rasterized font RESOURCE: a glyph atlas of a single face (family × weight) at a reference
// pixelSize; at draw time it is scaled to the required point size and reused for all sizes.
// It is registered in Context (setFontAtlas) and resolved by descriptor. What a widget is given is
// the lightweight value descriptor Font (below), not this resource.
class FontAtlas final {
public:
    struct AtlasData {
        const unsigned char *rgbaPixels = nullptr;
        uint16_t width = 0;
        uint16_t height = 0;
    };

    using CreateAtlasTextureFn = TextureHandle (*)(void *userData, const AtlasData &atlas);

    struct Glyph {
        float x0 = 0.f;
        float y0 = 0.f;
        float x1 = 0.f;
        float y1 = 0.f;
        float u0 = 0.f;
        float v0 = 0.f;
        float u1 = 0.f;
        float v1 = 0.f;
        float advanceX = 0.f;
    };

    // CPU-rasterized atlas: everything that is computed WITHOUT the GPU (can run on a worker thread). The
    // GPU texture is created separately on the main thread (createFromCpuAtlas) — Miren is single-threaded.
    struct CpuAtlas {
        std::vector<unsigned char> rgbaPixels;
        std::unordered_map<uint32_t, Glyph> glyphs;
        Rect whiteTextureCoords;
        int dim = 0;
        float pixelSize = 0.f;
        float ascent = 0.f;
        float descent = 0.f;
        float lineHeight = 0.f;
        bool valid = false;
    };

    static std::shared_ptr<FontAtlas> createFromMemory(
        const void *data, uint32_t size, float pixelSize, CreateAtlasTextureFn createTexture, void *userData
    );

    // Two-phase build for parallel loading: rasterizeFromMemory — pure CPU (thread-safe, no GPU),
    // createFromCpuAtlas — texture upload on the main thread. createFromMemory = the two glued together.
    static CpuAtlas rasterizeFromMemory(const void *data, uint32_t size, float pixelSize);
    static std::shared_ptr<FontAtlas>
    createFromCpuAtlas(CpuAtlas &&cpu, CreateAtlasTextureFn createTexture, void *userData);

    const Glyph *findGlyph(uint32_t codepoint) const;
    Size measureText(std::string_view text, float fontSize = 0.f) const;

    TextureHandle getTexture() const {
        return m_texture;
    }

    Rect getWhiteTextureCoords() const {
        return m_whiteTextureCoords;
    }

    float getPixelSize() const {
        return m_pixelSize;
    }

    float getAscent() const {
        return m_ascent;
    }

    float getLineHeight() const {
        return m_lineHeight;
    }

private:
    TextureHandle m_texture;
    Rect m_whiteTextureCoords;
    std::unordered_map<uint32_t, Glyph> m_glyphs;
    float m_pixelSize = 0.f;
    float m_ascent = 0.f;
    float m_descent = 0.f;
    float m_lineHeight = 0.f;
    uint32_t m_fallbackCodepoint = '?';
};

// Lightweight font value DESCRIPTOR (like UIFont): family + weight + point size. This is what a widget
// receives through setFont(Font). The atlas itself (FontAtlas) is resolved from Context at draw time by
// (family, weight); size is applied as a scale. Tracking/line spacing are NOT part of the font (they are
// paragraph attributes and live as separate Label properties, like NSParagraphStyle in UIKit).
struct Font {
    FontFamily family = FontFamily::Sans;
    FontWeight weight = FontWeight::Regular;
    float size = 16.f;
    // Synthetic italic: the font set has no italic faces, the slant is produced by glyph rendering
    // (a skew from the baseline). The atlas is resolved by the same (family, weight).
    bool italic = false;

    Font() = default;
    constexpr Font(float pointSize, FontWeight fontWeight = FontWeight::Regular, FontFamily fontFamily = FontFamily::Sans)
        : family(fontFamily)
        , weight(fontWeight)
        , size(pointSize) {}

    constexpr Font withSize(float newSize) const {
        Font copy = *this;
        copy.size = newSize;
        return copy;
    }
    constexpr Font withWeight(FontWeight newWeight) const {
        Font copy = *this;
        copy.weight = newWeight;
        return copy;
    }
    constexpr Font withFamily(FontFamily newFamily) const {
        Font copy = *this;
        copy.family = newFamily;
        return copy;
    }
    constexpr Font withItalic(bool newItalic = true) const {
        Font copy = *this;
        copy.italic = newItalic;
        return copy;
    }
    // Monospaced variant with the same point size/weight (like UIFont.monospacedSystemFont).
    static constexpr Font monospaced(float pointSize, FontWeight fontWeight = FontWeight::Regular) {
        return Font(pointSize, fontWeight, FontFamily::Mono);
    }
};

} // namespace PandaUI
