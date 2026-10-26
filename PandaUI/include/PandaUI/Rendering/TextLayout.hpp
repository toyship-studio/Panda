#pragma once

#include "PandaUI/Core/Base.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace PandaUI {

class FontAtlas;

enum class TextAlignment {
    Left,
    Center,
    Right,
};

// Font weight. The context holds one Font per weight; elements pick the one they need
// (headings — Bold, small/secondary text — Medium, body — Regular). A missing weight
// falls back to Regular.
enum class FontWeight {
    Regular,
    Medium,
    Bold,
};

// The id of a font family: FNV-1a of its name.
constexpr uint32_t fontFamilyId(std::string_view name) {
    uint32_t hash = 2166136261u;
    for (char c : name) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 16777619u;
    }
    return hash;
}

// Font family. The two ROLES the UI is designed with — Sans, the UI font, and Mono for numbers,
// paths, identifiers, code — and any family registered under its own name: fontFamily("Cinzel").
// Which real font plays a role is the host's decision (the editor: a setting; the bundled Inter
// Display and JetBrains Mono by default). The context holds an atlas per (family, weight); a
// missing pair falls back: the family's Regular → Sans of the same weight → Sans Regular.
enum class FontFamily : uint32_t {
    Sans = fontFamilyId("sans"),
    Mono = fontFamilyId("mono"),
};
constexpr FontFamily fontFamily(std::string_view name) {
    return static_cast<FontFamily>(fontFamilyId(name));
}

enum class TextVerticalAlignment {
    Top,
    Center,
    Bottom,
};

enum class TextOverflowMode {
    Clip,
    TailEllipsis,
};

struct TextLine {
    size_t start = 0;
    size_t length = 0;
    Size size;
    std::string displayText;
};

struct TextLayoutOptions {
    float fontSize = 0.f;
    float maxWidth = 0.f;
    int numberOfLines = 1;
    float lineSpacing = 0.f;
    float letterSpacing = 0.f; // letter tracking in px (uppercase panel/section headers)
    TextOverflowMode overflowMode = TextOverflowMode::TailEllipsis;
    bool trimsTrailingWhitespace = true;
};

struct TextLayoutResult {
    Size size;
    float lineHeight = 0.f;
    float lineSpacing = 0.f;
    bool didTruncate = false;
    std::vector<TextLine> lines;
};

class TextLayout final {
public:
    static TextLayoutResult layout(const FontAtlas &font, std::string_view text, const TextLayoutOptions &options);
    static Size measure(const FontAtlas &font, std::string_view text, const TextLayoutOptions &options);
};

} // namespace PandaUI
