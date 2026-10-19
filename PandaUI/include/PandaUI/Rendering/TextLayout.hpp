#pragma once

#include "PandaUI/Core/Base.hpp"

#include <cstddef>
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

// Font family. Sans — the main UI font (Inter Display); Mono — monospaced (JetBrains Mono) for
// numbers, paths, identifiers, axes, FPS, hotkeys. The context holds one Font per
// (family, weight) pair; a missing pair falls back: Mono without the weight → Mono Regular → Sans.
enum class FontFamily {
    Sans,
    Mono,
};

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
