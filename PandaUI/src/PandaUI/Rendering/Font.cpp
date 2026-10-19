#include "PandaUI/Rendering/Font.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <vector>

namespace PandaUI {

namespace {

    // The "General Punctuation" range (U+2010..U+2027): dashes – —, quotes ‘ ’ “ ”, ellipsis …, bullet •.
    constexpr uint32_t PUNCTUATION_FIRST = 0x2010;
    constexpr uint32_t PUNCTUATION_LAST = 0x2027;

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

    void appendGlyphs(
        std::unordered_map<uint32_t, FontAtlas::Glyph> &glyphs,
        const std::vector<stbtt_packedchar> &packedChars,
        uint32_t firstCodepoint,
        int atlasWidth,
        int atlasHeight
    ) {
        for (size_t i = 0; i < packedChars.size(); ++i) {
            const stbtt_packedchar &packed = packedChars[i];
            stbtt_aligned_quad quad;
            float unusedX = 0.f;
            float unusedY = 0.f;
            stbtt_GetPackedQuad(
                packedChars.data(), atlasWidth, atlasHeight, static_cast<int>(i), &unusedX, &unusedY, &quad, 0
            );

            FontAtlas::Glyph glyph;
            glyph.x0 = quad.x0;
            glyph.y0 = quad.y0;
            glyph.x1 = quad.x1;
            glyph.y1 = quad.y1;
            glyph.u0 = quad.s0;
            glyph.v0 = quad.t0;
            glyph.u1 = quad.s1;
            glyph.v1 = quad.t1;
            glyph.advanceX = packed.xadvance;
            glyphs[firstCodepoint + static_cast<uint32_t>(i)] = glyph;
        }
    }

} // namespace

std::shared_ptr<FontAtlas> FontAtlas::createFromMemory(
    const void *data, uint32_t size, float pixelSize, CreateAtlasTextureFn createTexture, void *userData
) {
    return createFromCpuAtlas(rasterizeFromMemory(data, size, pixelSize), createTexture, userData);
}

std::shared_ptr<FontAtlas>
FontAtlas::createFromCpuAtlas(CpuAtlas &&cpu, CreateAtlasTextureFn createTexture, void *userData) {
    if (!cpu.valid || !createTexture) { return nullptr; }
    auto font = std::make_shared<FontAtlas>();
    font->m_pixelSize = cpu.pixelSize;
    font->m_ascent = cpu.ascent;
    font->m_descent = cpu.descent;
    font->m_lineHeight = cpu.lineHeight;
    font->m_glyphs = std::move(cpu.glyphs);
    font->m_whiteTextureCoords = cpu.whiteTextureCoords;

    AtlasData atlas;
    atlas.rgbaPixels = cpu.rgbaPixels.data();
    atlas.width = static_cast<uint16_t>(cpu.dim);
    atlas.height = static_cast<uint16_t>(cpu.dim);
    font->m_texture = createTexture(userData, atlas);
    if (!font->m_texture.isValid()) { return nullptr; }
    return font;
}

FontAtlas::CpuAtlas FontAtlas::rasterizeFromMemory(const void *data, uint32_t size, float pixelSize) {
    CpuAtlas cpu;
    if (!data || size == 0) { return cpu; }

    const auto *ttfData = static_cast<const unsigned char *>(data);
    const int fontOffset = stbtt_GetFontOffsetForIndex(ttfData, 0);
    stbtt_fontinfo fontInfo;
    if (fontOffset < 0 || !stbtt_InitFont(&fontInfo, ttfData, fontOffset)) { return cpu; }

    // pixelSize is noticeably larger than the logical point size so that text stays sharp when enlarged
    // (headings, retina 2x — the geometry is in physical pixels). drawText normalizes a glyph through
    // scale = fontSize / pixelSize.
    std::vector<stbtt_packedchar> latinGlyphs(224);
    std::vector<stbtt_packedchar> cyrillicGlyphs(0x052F - 0x0400 + 1);
    std::vector<stbtt_packedchar> punctuationGlyphs(PUNCTUATION_LAST - PUNCTUATION_FIRST + 1);

    std::array<stbtt_pack_range, 3> ranges{};
    ranges[0].font_size = pixelSize;
    ranges[0].first_unicode_codepoint_in_range = 32;
    ranges[0].num_chars = static_cast<int>(latinGlyphs.size());
    ranges[0].chardata_for_range = latinGlyphs.data();
    ranges[1].font_size = pixelSize;
    ranges[1].first_unicode_codepoint_in_range = 0x0400;
    ranges[1].num_chars = static_cast<int>(cyrillicGlyphs.size());
    ranges[1].chardata_for_range = cyrillicGlyphs.data();
    ranges[2].font_size = pixelSize;
    ranges[2].first_unicode_codepoint_in_range = static_cast<int>(PUNCTUATION_FIRST);
    ranges[2].num_chars = static_cast<int>(punctuationGlyphs.size());
    ranges[2].chardata_for_range = punctuationGlyphs.data();

    // The atlas is taken as small as is sufficient: with 1×1 oversampling the glyphs fit into 1024², which
    // saves 4× memory on the buffer/expansion/copy (the main cost of loading) compared to 2048².
    // If they do not fit (a large point size) — grow to 2048².
    std::vector<unsigned char> alphaPixels;
    int atlasDim = 0;
    for (const int candidate : {1024, 2048}) {
        alphaPixels.assign(static_cast<size_t>(candidate) * candidate, 0);
        stbtt_pack_context packContext;
        if (!stbtt_PackBegin(&packContext, alphaPixels.data(), candidate, candidate, 0, 1, nullptr)) { continue; }
        stbtt_PackSetOversampling(&packContext, 1, 1);
        const int packed =
            stbtt_PackFontRanges(&packContext, ttfData, 0, ranges.data(), static_cast<int>(ranges.size()));
        stbtt_PackEnd(&packContext);
        if (packed) {
            atlasDim = candidate;
            break;
        }
    }
    if (atlasDim == 0) { return cpu; }

    int unscaledAscent = 0;
    int unscaledDescent = 0;
    int unscaledLineGap = 0;
    const float scale = stbtt_ScaleForPixelHeight(&fontInfo, pixelSize);
    stbtt_GetFontVMetrics(&fontInfo, &unscaledAscent, &unscaledDescent, &unscaledLineGap);

    cpu.pixelSize = pixelSize;
    cpu.ascent = static_cast<float>(unscaledAscent) * scale;
    cpu.descent = static_cast<float>(unscaledDescent) * scale;
    cpu.lineHeight = static_cast<float>(unscaledAscent - unscaledDescent + unscaledLineGap) * scale;
    appendGlyphs(cpu.glyphs, latinGlyphs, 32, atlasDim, atlasDim);
    appendGlyphs(cpu.glyphs, cyrillicGlyphs, 0x0400, atlasDim, atlasDim);
    appendGlyphs(cpu.glyphs, punctuationGlyphs, PUNCTUATION_FIRST, atlasDim, atlasDim);

    cpu.rgbaPixels.resize(static_cast<size_t>(atlasDim) * atlasDim * 4);
    for (size_t i = 0; i < alphaPixels.size(); ++i) {
        cpu.rgbaPixels[i * 4 + 0] = 255;
        cpu.rgbaPixels[i * 4 + 1] = 255;
        cpu.rgbaPixels[i * 4 + 2] = 255;
        cpu.rgbaPixels[i * 4 + 3] = alphaPixels[i];
    }

    cpu.rgbaPixels[0] = 255;
    cpu.rgbaPixels[1] = 255;
    cpu.rgbaPixels[2] = 255;
    cpu.rgbaPixels[3] = 255;
    cpu.whiteTextureCoords.origin =
        Point(0.5f / static_cast<float>(atlasDim), 0.5f / static_cast<float>(atlasDim));
    cpu.whiteTextureCoords.size = Size(0.f, 0.f);

    cpu.dim = atlasDim;
    cpu.valid = true;
    return cpu;
}

const FontAtlas::Glyph *FontAtlas::findGlyph(uint32_t codepoint) const {
    auto it = m_glyphs.find(codepoint);
    if (it != m_glyphs.end()) { return &it->second; }

    it = m_glyphs.find(m_fallbackCodepoint);
    if (it != m_glyphs.end()) { return &it->second; }

    return nullptr;
}

Size FontAtlas::measureText(std::string_view text, float fontSize) const {
    if (fontSize <= 0.f) { fontSize = m_pixelSize; }
    const float scale = fontSize / m_pixelSize;
    float lineWidth = 0.f;
    float maxWidth = 0.f;
    int lines = 1;

    for (size_t i = 0; i < text.size();) {
        const uint32_t codepoint = decodeUtf8(text, i);
        if (codepoint == '\n') {
            maxWidth = std::max(maxWidth, lineWidth);
            lineWidth = 0.f;
            ++lines;
            continue;
        }

        const Glyph *glyph = findGlyph(codepoint);
        if (glyph) { lineWidth += glyph->advanceX * scale; }
    }

    maxWidth = std::max(maxWidth, lineWidth);
    return {maxWidth, m_lineHeight * scale * static_cast<float>(lines)};
}

} // namespace PandaUI
