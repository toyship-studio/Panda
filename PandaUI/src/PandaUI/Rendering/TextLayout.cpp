#include "PandaUI/Rendering/TextLayout.hpp"

#include "PandaUI/Rendering/Font.hpp"
#include "PandaUI/Core/Profile.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace PandaUI {

namespace {

    constexpr std::string_view ELLIPSIS = "...";

    struct TextRun {
        size_t byteStart = 0;
        size_t byteEnd = 0;
        uint32_t codepoint = 0;
        float advance = 0.f;
    };

    struct LineSlice {
        size_t begin = 0;
        size_t end = 0;
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

    bool isNewline(uint32_t codepoint) {
        return codepoint == '\n';
    }

    bool isWrappingWhitespace(uint32_t codepoint) {
        return codepoint == ' ' || codepoint == '\t';
    }

    // A glyph the face lacks comes from its fallback chain, scaled by that atlas: the advance asks the
    // atlas itself.
    float advanceForCodepoint(const FontAtlas &font, uint32_t codepoint, float fontSize) {
        if (isNewline(codepoint)) { return 0.f; }
        return font.advance(codepoint, fontSize);
    }

    std::vector<TextRun> buildRuns(const FontAtlas &font, std::string_view text, float fontSize, float letterSpacing) {
        std::vector<TextRun> runs;
        runs.reserve(text.size());

        for (size_t index = 0; index < text.size();) {
            const size_t byteStart = index;
            const uint32_t codepoint = decodeUtf8(text, index);

            TextRun run;
            run.byteStart = byteStart;
            run.byteEnd = index;
            run.codepoint = codepoint;
            run.advance = advanceForCodepoint(font, codepoint, fontSize);
            if (!isNewline(codepoint)) { run.advance += letterSpacing; } // tracking is part of the glyph width
            runs.push_back(run);
        }

        return runs;
    }

    float widthOfRuns(const std::vector<TextRun> &runs, size_t begin, size_t end) {
        float width = 0.f;
        for (size_t index = begin; index < end; ++index) {
            width += runs[index].advance;
        }
        return width;
    }

    float widthOfText(const FontAtlas &font, std::string_view text, float fontSize, float letterSpacing) {
        float width = 0.f;
        for (size_t index = 0; index < text.size();) {
            const uint32_t codepoint = decodeUtf8(text, index);
            width += advanceForCodepoint(font, codepoint, fontSize);
            if (!isNewline(codepoint)) { width += letterSpacing; }
        }
        return width;
    }

    size_t trimTrailingWhitespace(const std::vector<TextRun> &runs, size_t begin, size_t end) {
        while (end > begin && isWrappingWhitespace(runs[end - 1].codepoint)) {
            --end;
        }
        return end;
    }

    size_t skipLeadingWhitespace(const std::vector<TextRun> &runs, size_t index) {
        while (index < runs.size() && isWrappingWhitespace(runs[index].codepoint)) {
            ++index;
        }
        return index;
    }

    std::vector<LineSlice> buildLineSlices(const std::vector<TextRun> &runs, std::string_view text, float maxWidth) {
        std::vector<LineSlice> slices;
        const bool hasWidthLimit = std::isfinite(maxWidth);

        size_t lineStart = 0;
        size_t lastBreak = std::numeric_limits<size_t>::max();
        float lineWidth = 0.f;

        for (size_t index = 0; index < runs.size();) {
            const TextRun &run = runs[index];
            if (isNewline(run.codepoint)) {
                slices.push_back({lineStart, index});
                ++index;
                lineStart = index;
                lastBreak = std::numeric_limits<size_t>::max();
                lineWidth = 0.f;
                continue;
            }

            const float nextLineWidth = lineWidth + run.advance;
            if (hasWidthLimit && lineWidth > 0.f && nextLineWidth > maxWidth) {
                size_t lineEnd = index;
                size_t nextLineStart = index;

                if (lastBreak != std::numeric_limits<size_t>::max() && lastBreak > lineStart) {
                    lineEnd = lastBreak;
                    nextLineStart = skipLeadingWhitespace(runs, lastBreak + 1);
                }

                slices.push_back({lineStart, lineEnd});
                lineStart = nextLineStart;
                index = nextLineStart;
                lastBreak = std::numeric_limits<size_t>::max();
                lineWidth = 0.f;
                continue;
            }

            lineWidth = nextLineWidth;
            if (isWrappingWhitespace(run.codepoint)) { lastBreak = index; }
            ++index;
        }

        const bool needsTrailingLine = text.empty() || (!runs.empty() && isNewline(runs.back().codepoint));
        if (lineStart < runs.size() || needsTrailingLine) { slices.push_back({lineStart, runs.size()}); }
        return slices;
    }

    std::string textForRuns(std::string_view text, const std::vector<TextRun> &runs, size_t begin, size_t end) {
        if (end <= begin || begin >= runs.size()) { return {}; }
        const size_t byteStart = runs[begin].byteStart;
        const size_t byteEnd = runs[end - 1].byteEnd;
        return std::string(text.substr(byteStart, byteEnd - byteStart));
    }

    TextLine makeLine(
        std::string_view text,
        const std::vector<TextRun> &runs,
        LineSlice slice,
        float lineHeight,
        bool trimsTrailingWhitespace
    ) {
        const size_t begin = slice.begin;
        const size_t end = trimsTrailingWhitespace ? trimTrailingWhitespace(runs, begin, slice.end) : slice.end;

        TextLine line;
        line.start = begin < runs.size() ? runs[begin].byteStart : text.size();
        line.length = end > begin ? runs[end - 1].byteEnd - line.start : 0;
        line.size = {widthOfRuns(runs, begin, end), lineHeight};
        return line;
    }

    TextLine makeEllipsizedLine(
        const FontAtlas &font,
        std::string_view text,
        const std::vector<TextRun> &runs,
        LineSlice slice,
        float maxWidth,
        float lineHeight,
        float fontSize,
        float letterSpacing
    ) {
        const size_t begin = slice.begin;
        size_t end = trimTrailingWhitespace(runs, begin, slice.end);
        const float ellipsisWidth = widthOfText(font, ELLIPSIS, fontSize, letterSpacing);

        if (std::isfinite(maxWidth)) {
            const float availableWidth = std::max(0.f, maxWidth);
            while (end > begin && widthOfRuns(runs, begin, end) + ellipsisWidth > availableWidth) {
                --end;
                end = trimTrailingWhitespace(runs, begin, end);
            }
        }

        TextLine line;
        line.start = begin < runs.size() ? runs[begin].byteStart : text.size();
        line.length = end > begin ? runs[end - 1].byteEnd - line.start : 0;
        line.displayText = textForRuns(text, runs, begin, end);
        line.displayText += ELLIPSIS;
        line.size = {widthOfText(font, line.displayText, fontSize, letterSpacing), lineHeight};
        return line;
    }

    void addLine(TextLayoutResult &result, TextLine line) {
        result.size.width = std::max(result.size.width, line.size.width);
        result.lines.push_back(std::move(line));
    }

} // namespace

TextLayoutResult TextLayout::layout(const FontAtlas &font, std::string_view text, const TextLayoutOptions &options) {
    PANDA_UI_PROFILE_SCOPE_N("PandaUI::TextLayout::layout");
    TextLayoutResult result;
    const float fontSize = options.fontSize > 0.f ? options.fontSize : font.getPixelSize();
    if (fontSize <= 0.f || font.getPixelSize() <= 0.f) { return result; }

    const float scale = fontSize / font.getPixelSize();
    const float lineHeight = font.getLineHeight() * scale;
    const float lineSpacing = std::max(0.f, options.lineSpacing);
    const float maxWidth = options.maxWidth > 0.f ? options.maxWidth : std::numeric_limits<float>::infinity();
    const std::vector<TextRun> runs = buildRuns(font, text, fontSize, options.letterSpacing);
    const std::vector<LineSlice> slices = buildLineSlices(runs, text, maxWidth);

    result.lineHeight = lineHeight;
    result.lineSpacing = lineSpacing;

    size_t visibleLineCount = slices.size();
    if (options.numberOfLines > 0 && visibleLineCount > static_cast<size_t>(options.numberOfLines)) {
        visibleLineCount = static_cast<size_t>(options.numberOfLines);
        result.didTruncate = true;
    }

    for (size_t index = 0; index < visibleLineCount; ++index) {
        const bool shouldEllipsize = result.didTruncate && index == visibleLineCount - 1 &&
                                     options.overflowMode == TextOverflowMode::TailEllipsis;
        addLine(
            result,
            shouldEllipsize ? makeEllipsizedLine(font, text, runs, slices[index], maxWidth, lineHeight, fontSize, options.letterSpacing)
                            : makeLine(text, runs, slices[index], lineHeight, options.trimsTrailingWhitespace)
        );
    }

    if (!result.lines.empty()) {
        result.size.height = lineHeight * static_cast<float>(result.lines.size()) +
                             lineSpacing * static_cast<float>(result.lines.size() - 1);
    }
    return result;
}

Size TextLayout::measure(const FontAtlas &font, std::string_view text, const TextLayoutOptions &options) {
    return layout(font, text, options).size;
}

} // namespace PandaUI
