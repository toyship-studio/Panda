#include "PandaUI/Elements/Label.hpp"

#include "PandaUI/Runtime/Context.hpp"
#include "PandaUI/Core/Profile.hpp"
#include "PandaUI/Core/Theme.hpp"
#include "PandaUI/Rendering/TextLayout.hpp"
#include "PandaUI/Rendering/UIRenderer.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace PandaUI {

LabelAppearance &Label::defaultAppearance() {
    static LabelAppearance appearance;
    return appearance;
}

Label::Label()
    : Label("") {}

Label::Label(std::string text)
    : m_text(std::move(text))
    , m_textColor(ThemeColor::textPrimary)
    , m_textRole(TextRole::Primary)
    , m_font(defaultAppearance().font)
    , m_numberOfLines(1)
    , m_lineSpacing(0.f)
    , m_textAlignment(TextAlignment::Left)
    , m_verticalTextAlignment(TextVerticalAlignment::Center)
    , m_textOverflowMode(TextOverflowMode::TailEllipsis)
    , m_cachedFont(nullptr)
    , m_cachedLayoutOptions()
    , m_cachedLayout()
    , m_cachedLayoutValid(false) {
    setBackgroundColor(Color(0x00000000));
    setUsesIntrinsicContentSize(true);
    layout().setFlexShrink(1.f);
    // The text is decorative: it does not take the hit-test, hover and cursor away from the container
    // control (Button/Combobox/list rows, etc.). Whoever needs interactive text enables it explicitly.
    setUserInteractionEnabled(false);
}

void Label::setText(std::string text) {
    if (m_text == text) { return; }
    m_text = std::move(text);
    invalidateLayoutCache();
    invalidateIntrinsicContentSize();
}

void Label::setTextColor(ThemeColor color) {
    m_textColor = color;
    setNeedsDisplay();
}

void Label::setTextRole(TextRole role) {
    m_textRole = role;
    m_textColor = roleColor(role);
    setNeedsDisplay();
}

ThemeColor Label::roleColor(TextRole role) {
    switch (role) {
        case TextRole::Primary:
            return ThemeColor::textPrimary;
        case TextRole::Secondary:
            return ThemeColor::textSecondary;
        case TextRole::Muted:
            return ThemeColor::textMuted;
        case TextRole::Dim:
            return ThemeColor::textDim;
        case TextRole::Disabled:
            return ThemeColor::textDisabled;
        case TextRole::Accent:
            return ThemeColor::accent;
    }
    return ThemeColor::textPrimary;
}

void Label::themeChanged() {
    // The color resolves at draw. The fonts may have changed with the appearance: the layout and
    // the measured size are made anew.
    invalidateLayoutCache();
    invalidateIntrinsicContentSize();
    setNeedsDisplay();
}

float Label::effectiveFontSize() const {
    const Context *context = getContext();
    return m_font.size * (context ? context->textScale() : 1.f);
}

void Label::setFont(Font font) {
    m_font = font; // family/weight/point size as a single value; the layout is always recomputed
    invalidateLayoutCache();
    invalidateIntrinsicContentSize();
}

void Label::setNumberOfLines(int numberOfLines) {
    numberOfLines = std::max(0, numberOfLines);
    if (m_numberOfLines == numberOfLines) { return; }
    m_numberOfLines = numberOfLines;
    invalidateLayoutCache();
    invalidateIntrinsicContentSize();
}

void Label::setTextAlignment(TextAlignment alignment) {
    if (m_textAlignment == alignment) { return; }
    m_textAlignment = alignment;
    setNeedsDisplay();
}

void Label::setVerticalTextAlignment(TextVerticalAlignment alignment) {
    if (m_verticalTextAlignment == alignment) { return; }
    m_verticalTextAlignment = alignment;
    setNeedsDisplay();
}

void Label::setLineSpacing(float lineSpacing) {
    lineSpacing = std::max(0.f, lineSpacing);
    if (m_lineSpacing == lineSpacing) { return; }
    m_lineSpacing = lineSpacing;
    invalidateLayoutCache();
    invalidateIntrinsicContentSize();
}

void Label::setLetterSpacing(float letterSpacing) {
    if (m_letterSpacing == letterSpacing) { return; }
    m_letterSpacing = letterSpacing;
    invalidateLayoutCache();
    invalidateIntrinsicContentSize();
}

void Label::setTextShadow(TextShadow shadow) {
    m_textShadow = shadow;
    setNeedsDisplay();
}

void Label::setTextOutline(TextOutline outline) {
    m_textOutline = outline;
    m_textOutline.width = std::max(0.f, m_textOutline.width);
    setNeedsDisplay();
}

void Label::setTextOverflowMode(TextOverflowMode overflowMode) {
    if (m_textOverflowMode == overflowMode) { return; }
    m_textOverflowMode = overflowMode;
    invalidateLayoutCache();
    invalidateIntrinsicContentSize();
}

Size Label::measureText() const {
    return intrinsicContentSize({});
}

Size Label::intrinsicContentSize(Size availableSize) const {
    Context *context = getContext();
    std::shared_ptr<FontAtlas> font = context ? context->getFontAtlas(m_font.weight, m_font.family) : nullptr;
    if (!font) { return {}; }

    return getLayout(*font, makeLayoutOptions(availableSize.width)).size;
}

TextLayoutOptions Label::makeLayoutOptions(float maxWidth) const {
    TextLayoutOptions options;
    options.fontSize = effectiveFontSize();
    options.maxWidth = maxWidth;
    options.numberOfLines = m_numberOfLines;
    options.lineSpacing = m_lineSpacing;
    options.letterSpacing = m_letterSpacing;
    options.overflowMode = m_textOverflowMode;
    return options;
}

const TextLayoutResult &Label::getLayout(const FontAtlas &font, const TextLayoutOptions &options) const {
    const bool cacheMatches = m_cachedLayoutValid && m_cachedFont == &font &&
                              m_cachedLayoutOptions.fontSize == options.fontSize &&
                              m_cachedLayoutOptions.maxWidth == options.maxWidth &&
                              m_cachedLayoutOptions.numberOfLines == options.numberOfLines &&
                              m_cachedLayoutOptions.lineSpacing == options.lineSpacing &&
                              m_cachedLayoutOptions.letterSpacing == options.letterSpacing &&
                              m_cachedLayoutOptions.overflowMode == options.overflowMode &&
                              m_cachedLayoutOptions.trimsTrailingWhitespace == options.trimsTrailingWhitespace;
    if (!cacheMatches) {
        m_cachedFont = &font;
        m_cachedLayoutOptions = options;
        m_cachedLayout = TextLayout::layout(font, m_text, options);
        m_cachedLayoutValid = true;
    }
    return m_cachedLayout;
}

void Label::invalidateLayoutCache() {
    m_cachedFont = nullptr;
    m_cachedLayout = {};
    m_cachedLayoutValid = false;
}

void Label::draw(float offsetX, float offsetY) {
    PANDA_UI_PROFILE_SCOPE_N("PandaUI::Label::draw");
    if (m_text.empty()) { return; }
    Context *context = getContext();
    if (!context) { return; }
    std::shared_ptr<FontAtlas> font = context->getFontAtlas(m_font.weight, m_font.family);
    if (!font) { return; }

    Rect frame = getFrame();
    // The text lives in the content box: the padding of the label itself (Yoga accounts for it when
    // measuring, but the drawing is done here) shifts the origin and narrows the available width.
    const float padLeft = this->layout().computedPadding(Edge::Left);
    const float padTop = this->layout().computedPadding(Edge::Top);
    const float contentWidth =
        std::max(0.f, frame.size.width - padLeft - this->layout().computedPadding(Edge::Right));
    const float contentHeight =
        std::max(0.f, frame.size.height - padTop - this->layout().computedPadding(Edge::Bottom));
    const TextLayoutResult &layout = getLayout(*font, makeLayoutOptions(contentWidth));
    float yOffset = padTop;
    switch (m_verticalTextAlignment) {
        case TextVerticalAlignment::Top:
            break;
        case TextVerticalAlignment::Center: {
            // A line taller than the box (the label is squashed in a fixed-height container) stays
            // centered: a clamp to zero pinned it to the top of the box, and the text drifted down relative
            // to the container. For multiline text a visible beginning matters more — the clamp is kept there.
            const float centered = (contentHeight - layout.size.height) * 0.5f;
            yOffset += layout.lines.size() <= 1 ? centered : std::max(0.f, centered);
            break;
        }
        case TextVerticalAlignment::Bottom:
            yOffset += std::max(0.f, contentHeight - layout.size.height);
            break;
    }

    // The decorations are more passes of the same glyphs: the outline around (eight directions, so
    // the stroke closes at any angle), then the shadow under, then the text itself.
    const bool outline = m_textOutline.isVisible();
    const bool shadow = m_textShadow.isVisible();
    const Color outlineColor = outline ? m_textOutline.color.resolve() : Color(0.f, 0.f, 0.f, 0.f);
    const Color shadowColor = shadow ? m_textShadow.color.resolve() : Color(0.f, 0.f, 0.f, 0.f);
    const Color textColor = m_textColor.resolve();
    const float fontSize = effectiveFontSize();
    UIRenderer &renderer = context->getRenderer();
    const auto drawPass = [&](std::string_view text, Point origin, Point offset, Color color) {
        renderer.drawText(
            *font, text, {origin.x + offset.x, origin.y + offset.y}, color, fontSize, m_letterSpacing, m_font.italic
        );
    };

    for (size_t lineIndex = 0; lineIndex < layout.lines.size(); ++lineIndex) {
        const TextLine &line = layout.lines[lineIndex];
        if (line.length == 0 && line.displayText.empty()) { continue; }

        float xOffset = padLeft;
        switch (m_textAlignment) {
            case TextAlignment::Left:
                break;
            case TextAlignment::Center:
                xOffset += std::max(0.f, (contentWidth - line.size.width) * 0.5f);
                break;
            case TextAlignment::Right:
                xOffset += std::max(0.f, contentWidth - line.size.width);
                break;
        }

        const std::string_view text = line.displayText.empty()
                                          ? std::string_view(m_text.data() + line.start, line.length)
                                          : std::string_view(line.displayText);
        const float lineY = yOffset + (layout.lineHeight + layout.lineSpacing) * static_cast<float>(lineIndex);
        const Point origin(offsetX + frame.origin.x + xOffset, offsetY + frame.origin.y + lineY);
        if (outline) {
            const float w = m_textOutline.width;
            const float d = w * 0.7071f;
            const Point offsets[8] = {{-w, 0.f}, {w, 0.f}, {0.f, -w}, {0.f, w}, {-d, -d}, {d, -d}, {-d, d}, {d, d}};
            for (const Point &offset : offsets) { drawPass(text, origin, offset, outlineColor); }
        }
        if (shadow) { drawPass(text, origin, m_textShadow.offset, shadowColor); }
        drawPass(text, origin, Point(0.f, 0.f), textColor);
    }
}

} // namespace PandaUI
