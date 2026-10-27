#include "PandaUI/Elements/TextField.hpp"

#include "PandaUI/Runtime/Context.hpp"
#include "PandaUI/Rendering/Font.hpp"
#include "PandaUI/Core/Clipboard.hpp"
#include "PandaUI/Core/Profile.hpp"
#include "PandaUI/Core/Theme.hpp"
#include "PandaUI/Rendering/UIRenderer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

namespace PandaUI {

namespace {

    // Lower bound of the field height. It does not define the height by itself — usually the text +
    // insets do; the bound only keeps the field from degenerating with a tiny font/insets, which
    // allows building compact fields (inspector, toolbars).
    constexpr double CARET_BLINK_INTERVAL = 0.5;
    constexpr double CLICK_CHAIN_INTERVAL = 0.5;
    constexpr float CLICK_CHAIN_DISTANCE = 5.f;

    enum class CharacterClass {
        Whitespace,
        Word,
        Punctuation,
    };

    double currentTimeSeconds() {
        using Clock = std::chrono::steady_clock;
        return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
    }

    bool isContinuationByte(unsigned char byte) {
        return (byte & 0xC0) == 0x80;
    }

    size_t previousUtf8Boundary(std::string_view text, size_t index) {
        if (index == 0) { return 0; }
        index = std::min(index, text.size()) - 1;
        while (index > 0 && isContinuationByte(static_cast<unsigned char>(text[index]))) {
            --index;
        }
        return index;
    }

    size_t nextUtf8Boundary(std::string_view text, size_t index) {
        if (index >= text.size()) { return text.size(); }
        ++index;
        while (index < text.size() && isContinuationByte(static_cast<unsigned char>(text[index]))) {
            ++index;
        }
        return index;
    }

    void appendUtf8(std::string &text, uint32_t codepoint) {
        if (codepoint <= 0x7F) {
            text.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FF) {
            text.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else if (codepoint <= 0xFFFF) {
            text.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else {
            text.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            text.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }

    uint32_t decodeUtf8At(std::string_view text, size_t index, size_t *nextIndex = nullptr) {
        if (index >= text.size()) {
            if (nextIndex) { *nextIndex = text.size(); }
            return 0;
        }

        const auto *bytes = reinterpret_cast<const unsigned char *>(text.data());
        size_t next = index + 1;
        const unsigned char first = bytes[index];
        uint32_t codepoint = first;

        if (first < 0x80) {
            codepoint = first;
        } else if ((first >> 5) == 0x6 && next < text.size()) {
            codepoint = ((first & 0x1F) << 6) | (bytes[next++] & 0x3F);
        } else if ((first >> 4) == 0xE && next + 1 < text.size()) {
            codepoint = ((first & 0x0F) << 12) | ((bytes[next] & 0x3F) << 6) | (bytes[next + 1] & 0x3F);
            next += 2;
        } else if ((first >> 3) == 0x1E && next + 2 < text.size()) {
            codepoint = ((first & 0x07) << 18) | ((bytes[next] & 0x3F) << 12)
                | ((bytes[next + 1] & 0x3F) << 6) | (bytes[next + 2] & 0x3F);
            next += 3;
        }

        if (nextIndex) { *nextIndex = next; }
        return codepoint;
    }

    CharacterClass characterClassFor(uint32_t codepoint) {
        if (codepoint == ' ' || codepoint == '\t' || codepoint == '\n' || codepoint == '\r') {
            return CharacterClass::Whitespace;
        }
        if ((codepoint >= 'a' && codepoint <= 'z') || (codepoint >= 'A' && codepoint <= 'Z')
            || (codepoint >= '0' && codepoint <= '9') || codepoint == '_' || codepoint >= 0x80) {
            return CharacterClass::Word;
        }
        return CharacterClass::Punctuation;
    }

    CharacterClass characterClassAt(std::string_view text, size_t index) {
        return characterClassFor(decodeUtf8At(text, index));
    }

    float widthOfTextRange(const FontAtlas &font, std::string_view text, size_t start, size_t end, float fontSize) {
        if (start >= end || start >= text.size()) { return 0.f; }
        end = std::min(end, text.size());
        return font.measureText(text.substr(start, end - start), fontSize).width;
    }

    float squaredDistance(Point lhs, Point rhs) {
        const float dx = lhs.x - rhs.x;
        const float dy = lhs.y - rhs.y;
        return dx * dx + dy * dy;
    }

} // namespace

TextField::TextField()
    : TextField("") {}

TextField::TextField(std::string text)
    : m_text(std::move(text))
    , m_placeholder()
    , m_onTextChanged(nullptr)
    , m_onSubmit(nullptr)
    , m_appearance(defaultAppearance())
    , m_textColor(m_appearance.text.resolve())
    , m_placeholderColor(m_appearance.placeholder.resolve())
    , m_caretColor(m_appearance.caret.resolve())
    , m_selectionColor(m_appearance.selection.resolve())
    , m_numberOfLines(1)
    , m_lineSpacing(3.f)
    , m_caretIndex(m_text.size())
    , m_selectionAnchorIndex(m_caretIndex)
    , m_caretBlinkElapsed(0.0)
    , m_caretVisible(true)
    , m_isSelectingWithPointer(false)
    , m_selectionPointerType(PointerType::Mouse)
    , m_selectionPointerId(0)
    , m_clickCount(0)
    , m_selectionClickCount(1)
    , m_lastClickTimestamp(0.0)
    , m_lastClickPosition()
    , m_pointerSelectionAnchorRange() {
    setCursor(Cursor::IBeam);
    setUsesIntrinsicContentSize(true);
    setClipsToBounds(true);
    layout().setFlexShrink(1.f);
    applyAppearance();
}

TextFieldAppearance &TextField::defaultAppearance() {
    static TextFieldAppearance appearance;
    return appearance;
}

void TextField::setAppearance(const TextFieldAppearance &appearance) {
    m_appearance = appearance;
    applyAppearance();
}

void TextField::applyAppearance() {
    surface().setCornerRadii(m_appearance.cornerRadius);
    surface().setBorderWidth(m_appearance.borderWidth);
    applyThemeColors();
    updateAppearance();
    invalidateTextLayout();
    invalidateIntrinsicContentSize();
    setNeedsDisplay();
}

void TextField::setText(std::string text) {
    if (m_text == text) { return; }
    m_text = std::move(text);
    m_caretIndex = std::min(m_caretIndex, m_text.size());
    m_selectionAnchorIndex = m_caretIndex;
    resetCaretBlink();
    invalidateTextLayout();
    invalidateIntrinsicContentSize();
}

bool TextField::hasSelection() const {
    return m_caretIndex != m_selectionAnchorIndex;
}

size_t TextField::getSelectionStart() const {
    return std::min(m_caretIndex, m_selectionAnchorIndex);
}

size_t TextField::getSelectionEnd() const {
    return std::max(m_caretIndex, m_selectionAnchorIndex);
}

void TextField::selectAll() {
    m_selectionAnchorIndex = 0;
    m_caretIndex = m_text.size();
    resetCaretBlink();
    setNeedsDisplay();
}

void TextField::setPlaceholder(std::string placeholder) {
    if (m_placeholder == placeholder) { return; }
    m_placeholder = std::move(placeholder);
    invalidateIntrinsicContentSize();
}

void TextField::setFont(Font font) {
    m_appearance.font = font; // family/weight/point size as a single value
    invalidateTextLayout();
    invalidateIntrinsicContentSize();
}

std::shared_ptr<FontAtlas> TextField::resolveFont() const {
    Context *context = getContext();
    return context ? context->getFontAtlas(m_appearance.font.weight, m_appearance.font.family) : nullptr;
}

void TextField::setNumberOfLines(int numberOfLines) {
    numberOfLines = std::max(0, numberOfLines);
    if (m_numberOfLines == numberOfLines) { return; }
    m_numberOfLines = numberOfLines;
    invalidateTextLayout();
    invalidateIntrinsicContentSize();
}

void TextField::setLineSpacing(float lineSpacing) {
    lineSpacing = std::max(0.f, lineSpacing);
    if (m_lineSpacing == lineSpacing) { return; }
    m_lineSpacing = lineSpacing;
    invalidateTextLayout();
    invalidateIntrinsicContentSize();
}

void TextField::setContentInsets(EdgeInsets insets) {
    if (m_appearance.contentInsets == insets) { return; }
    m_appearance.contentInsets = insets;
    invalidateIntrinsicContentSize();
}

void TextField::setTextAlignment(TextAlignment alignment) {
    if (m_alignment == alignment) { return; }
    m_alignment = alignment;
    setNeedsDisplay();
}

void TextField::setOnTextChanged(TextChangedCallback callback) {
    m_onTextChanged = std::move(callback);
}

void TextField::setOnSubmit(SubmitCallback callback) {
    m_onSubmit = std::move(callback);
}

void TextField::update(double deltaTime) {
    if (!isFocused() || !isEnabled() || hasSelection()) {
        m_caretBlinkElapsed = 0.0;
        m_caretVisible = true;
        return;
    }

    m_caretBlinkElapsed += std::max(0.0, deltaTime);
    while (m_caretBlinkElapsed >= CARET_BLINK_INTERVAL) {
        m_caretBlinkElapsed -= CARET_BLINK_INTERVAL;
        m_caretVisible = !m_caretVisible;
        setNeedsDisplay();
    }
}

bool TextField::pointerDown(PointerEvent &event) {
    if (!acceptsPointer(event)) { return false; }

    m_isSelectingWithPointer = true;
    m_selectionPointerType = event.type;
    m_selectionPointerId = event.pointerId;
    m_selectionClickCount = nextClickCount(event.position);
    m_pointerSelectionAnchorRange = selectionRangeAt(event.position, m_selectionClickCount);
    if (m_selectionClickCount == 1) {
        setCaretIndex(m_pointerSelectionAnchorRange.start, false);
    } else {
        selectRange(m_pointerSelectionAnchorRange);
    }
    setHighlighted(true);
    return true;
}

bool TextField::pointerUp(PointerEvent &event) {
    if (!isSelectionPointer(event)) { return false; }
    if (m_selectionClickCount == 1) {
        placeCaret(event.position, true);
    } else {
        extendSelectionToRange(selectionRangeAt(event.position, m_selectionClickCount));
    }
    m_isSelectingWithPointer = false;
    m_selectionClickCount = 1;
    setHighlighted(false);
    return true;
}

bool TextField::pointerMove(PointerEvent &event) {
    if (!isSelectionPointer(event)) { return false; }
    if (m_selectionClickCount == 1) {
        placeCaret(event.position, true);
    } else {
        extendSelectionToRange(selectionRangeAt(event.position, m_selectionClickCount));
    }
    setHighlighted(pointInside(event.position));
    return true;
}

bool TextField::keyDown(KeyEvent &event) {
    if (!isEnabled()) { return false; }

    const bool commandModifier = event.isSuperDown || event.isControlDown;
    if (commandModifier && event.key == Key::A) {
        selectAll();
        return true;
    }
    if (commandModifier && event.key == Key::C) {
        copySelection();
        return true;
    }
    if (commandModifier && event.key == Key::X) {
        cutSelection();
        return true;
    }
    if (commandModifier && event.key == Key::V) {
        pasteClipboard();
        return true;
    }

    if (commandModifier) {
        switch (event.key) {
            case Key::Left:
                moveCaretToLineStart(event.isShiftDown);
                return true;
            case Key::Right:
                moveCaretToLineEnd(event.isShiftDown);
                return true;
            case Key::Up:
                moveCaretToTextStart(event.isShiftDown);
                return true;
            case Key::Down:
                moveCaretToTextEnd(event.isShiftDown);
                return true;
            default:
                break;
        }
    }

    switch (event.key) {
        case Key::Backspace:
            deleteBackward();
            return true;
        case Key::Delete:
            deleteForward();
            return true;
        case Key::Left:
            moveCaretLeft(event.isShiftDown);
            return true;
        case Key::Right:
            moveCaretRight(event.isShiftDown);
            return true;
        case Key::Up:
            if (!isMultiline()) { return false; }
            moveCaretVertically(-1, event.isShiftDown);
            return true;
        case Key::Down:
            if (!isMultiline()) { return false; }
            moveCaretVertically(1, event.isShiftDown);
            return true;
        case Key::Home:
            moveCaretToLineStart(event.isShiftDown);
            return true;
        case Key::End:
            moveCaretToLineEnd(event.isShiftDown);
            return true;
        case Key::Enter:
        case Key::KeypadEnter:
            if (isMultiline()) {
                insertText("\n");
            } else {
                submit();
            }
            return true;
        default:
            return false;
    }
}

bool TextField::textInput(TextInputEvent &event) {
    if (!isEnabled() || event.codepoint == 0) { return false; }
    if (event.codepoint < 32 && event.codepoint != '\n') { return false; }
    if (event.codepoint == '\n' && !isMultiline()) { return false; }

    std::string input;
    appendUtf8(input, event.codepoint);
    insertText(input);
    return true;
}

void TextField::interactionCancelled() {
    Control::interactionCancelled();
    m_isSelectingWithPointer = false;
}

Size TextField::intrinsicContentSize(Size availableSize) const {
    std::shared_ptr<FontAtlas> font = resolveFont();
    if (!font) { return {160.f, m_appearance.minHeight}; }

    const float availableTextWidth =
        availableSize.width > 0.f ? std::max(0.f, availableSize.width - m_appearance.contentInsets.left - m_appearance.contentInsets.right)
                                  : 0.f;
    const std::string_view measuredText = m_text.empty() ? std::string_view(m_placeholder) : std::string_view(m_text);
    Size textSize = layoutText(*font, measuredText, isMultiline() ? availableTextWidth : 0.f).size;
    if (textSize.height <= 0.f) { textSize.height = font->getLineHeight() * (effectiveFontSize() / font->getPixelSize()); }

    return {
        textSize.width + m_appearance.contentInsets.left + m_appearance.contentInsets.right,
        std::max(m_appearance.minHeight, textSize.height + m_appearance.contentInsets.top + m_appearance.contentInsets.bottom),
    };
}

void TextField::draw(float offsetX, float offsetY) {
    PANDA_UI_PROFILE_SCOPE_N("PandaUI::TextField::draw");
    Context *context = getContext();
    if (!context) { return; }
    std::shared_ptr<FontAtlas> font = resolveFont();
    if (!font) { return; }

    Rect frame = getFrame();
    const float maxWidth = textMaxWidth(frame.size);
    // A single layout of m_text per frame serves the selection and caret, and — when the text is
    // non-empty — the body as well.
    const TextLayoutResult &textLayout = cachedTextLayout(*font, maxWidth);

    const bool showPlaceholder = m_text.empty();
    const Color visibleColor = showPlaceholder ? m_placeholderColor : m_textColor;
    const std::string_view bodyText = showPlaceholder ? std::string_view(m_placeholder) : std::string_view(m_text);
    TextLayoutResult placeholderLayout;
    if (showPlaceholder) { placeholderLayout = layoutText(*font, m_placeholder, maxWidth); }
    const TextLayoutResult &bodyLayout = showPlaceholder ? placeholderLayout : textLayout;

    // single-line: the caret is kept within the visible area, a long line is scrolled horizontally.
    m_scrollOffsetX = clampedScrollOffsetX(*font, textLayout, frame.size);
    const float textX =
        offsetX + frame.origin.x + m_appearance.contentInsets.left + textOriginX(bodyLayout, frame.size) - m_scrollOffsetX;
    const float textY = offsetY + frame.origin.y + m_appearance.contentInsets.top + textOriginY(frame.size, bodyLayout);
    if (isFocused() && hasSelection() && !m_text.empty()) {
        drawSelection(*font, textLayout, {textX, textY});
    }

    for (size_t lineIndex = 0; lineIndex < bodyLayout.lines.size(); ++lineIndex) {
        const TextLine &line = bodyLayout.lines[lineIndex];
        if (line.length == 0 && line.displayText.empty()) { continue; }

        const std::string_view lineText =
            line.displayText.empty() ? bodyText.substr(line.start, line.length) : std::string_view(line.displayText);
        const float lineY = textY + (bodyLayout.lineHeight + bodyLayout.lineSpacing) * static_cast<float>(lineIndex);
        context->getRenderer().drawText(*font, lineText, {textX, lineY}, visibleColor, effectiveFontSize());
    }

    if (!isFocused() || !isEnabled() || hasSelection() || !m_caretVisible) { return; }

    if (textLayout.lines.empty()) { return; }
    const size_t caretLineIndex = lineIndexForCaret(textLayout);
    const TextLine &caretLine = textLayout.lines[caretLineIndex];
    const float caretX = textX + caretXInLine(*font, caretLine, m_caretIndex);
    const float caretY = textY + (textLayout.lineHeight + textLayout.lineSpacing) * static_cast<float>(caretLineIndex);

    UIRenderer::RectData caret;
    caret.center = {caretX + m_appearance.caretWidth * 0.5f, caretY + textLayout.lineHeight * 0.5f, 0.f};
    caret.size = {m_appearance.caretWidth, textLayout.lineHeight};
    caret.color = m_caretColor;
    context->getRenderer().drawRect(caret);
}

void TextField::controlStateChanged(ControlState, ControlState) {
    updateAppearance();
    resetCaretBlink();
}

TextLayoutOptions TextField::makeLayoutOptions(float maxWidth) const {
    TextLayoutOptions options;
    options.fontSize = effectiveFontSize();
    options.maxWidth = maxWidth;
    options.numberOfLines = m_numberOfLines;
    options.lineSpacing = m_lineSpacing;
    options.overflowMode = TextOverflowMode::Clip;
    options.trimsTrailingWhitespace = false;
    return options;
}

TextLayoutResult TextField::layoutText(const FontAtlas &font, std::string_view text, float maxWidth) const {
    return TextLayout::layout(font, text, makeLayoutOptions(maxWidth));
}

const TextLayoutResult &TextField::cachedTextLayout(const FontAtlas &font, float maxWidth) const {
    if (!m_layoutCacheValid || m_cachedLayoutFont != &font || m_cachedLayoutMaxWidth != maxWidth) {
        m_cachedLayout = layoutText(font, m_text, maxWidth);
        m_cachedLayoutFont = &font;
        m_cachedLayoutMaxWidth = maxWidth;
        m_layoutCacheValid = true;
    }
    return m_cachedLayout;
}

void TextField::invalidateTextLayout() {
    m_layoutCacheValid = false;
}

void TextField::applyThemeColors() {
    m_textColor = m_appearance.text.resolve();
    m_placeholderColor = m_appearance.placeholder.resolve();
    m_caretColor = m_appearance.caret.resolve();
    m_selectionColor = m_appearance.selection.resolve();
}

void TextField::themeChanged() {
    applyThemeColors();
    updateAppearance();
    invalidateTextLayout(); // the fonts may have changed with the appearance
    invalidateIntrinsicContentSize();
    setNeedsDisplay();
}

float TextField::effectiveFontSize() const {
    const Context *context = getContext();
    return m_appearance.font.size * (context ? context->textScale() : 1.f);
}

void TextField::updateAppearance() {
    // The recessed input plate; in the editor only the border reacts to the state.
    const SurfaceStyle &style = hasControlState(ControlState::Disabled) ? m_appearance.disabled
                                : hasControlState(ControlState::Focused)  ? m_appearance.focused
                                : hasControlState(ControlState::Hovered)  ? m_appearance.hovered
                                                                          : m_appearance.normal;
    style.applyTo(surface());
}

void TextField::textDidChange() {
    invalidateTextLayout();
    invalidateIntrinsicContentSize();
    sendActions(ControlEvent::ValueChanged);
    if (m_onTextChanged) { m_onTextChanged(*this); }
}

void TextField::submit() {
    sendActions(ControlEvent::PrimaryActionTriggered);
    if (m_onSubmit) { m_onSubmit(*this); }
}

void TextField::insertText(std::string_view text) {
    if (text.empty()) { return; }
    replaceSelectionWith(text);
}

void TextField::copySelection() const {
    if (!hasSelection()) { return; }
    setClipboardText(std::string_view(m_text).substr(getSelectionStart(), getSelectionEnd() - getSelectionStart()));
}

void TextField::cutSelection() {
    if (!hasSelection()) { return; }
    copySelection();
    replaceSelectionWith("");
}

void TextField::pasteClipboard() {
    const std::string sanitized = sanitizePastedText(getClipboardText());
    if (sanitized.empty()) { return; }
    insertText(sanitized);
}

std::string TextField::sanitizePastedText(std::string_view text) const {
    // Printable codepoints (>= 0x20) are kept, and '\n' only for multiline; other
    // control characters are dropped — same as the input filter in textInput.
    std::string result;
    result.reserve(text.size());
    for (size_t index = 0; index < text.size();) {
        size_t next = index;
        const uint32_t codepoint = decodeUtf8At(text, index, &next);
        if (codepoint == '\n') {
            if (isMultiline()) { result.push_back('\n'); }
        } else if (codepoint >= 0x20) {
            result.append(text.substr(index, next - index));
        }
        index = next;
    }
    return result;
}

void TextField::replaceSelectionWith(std::string_view text) {
    const size_t selectionStart = getSelectionStart();
    const size_t selectionEnd = getSelectionEnd();
    if (hasSelection()) {
        m_text.replace(selectionStart, selectionEnd - selectionStart, text);
        m_caretIndex = selectionStart + text.size();
    } else {
        m_text.insert(m_caretIndex, text);
        m_caretIndex += text.size();
    }
    m_selectionAnchorIndex = m_caretIndex;
    resetCaretBlink();
    textDidChange();
}

void TextField::resetCaretBlink() {
    m_caretBlinkElapsed = 0.0;
    m_caretVisible = true;
    setNeedsDisplay();
}

void TextField::deleteBackward() {
    if (hasSelection()) {
        replaceSelectionWith("");
        return;
    }
    if (m_caretIndex == 0) { return; }
    const size_t previous = previousUtf8Boundary(m_text, m_caretIndex);
    m_text.erase(previous, m_caretIndex - previous);
    m_caretIndex = previous;
    m_selectionAnchorIndex = m_caretIndex;
    resetCaretBlink();
    textDidChange();
}

void TextField::deleteForward() {
    if (hasSelection()) {
        replaceSelectionWith("");
        return;
    }
    if (m_caretIndex >= m_text.size()) { return; }
    const size_t next = nextUtf8Boundary(m_text, m_caretIndex);
    m_text.erase(m_caretIndex, next - m_caretIndex);
    m_selectionAnchorIndex = m_caretIndex;
    resetCaretBlink();
    textDidChange();
}

void TextField::moveCaretLeft(bool extendsSelection) {
    if (hasSelection() && !extendsSelection) {
        setCaretIndex(getSelectionStart(), false);
    } else {
        setCaretIndex(previousUtf8Boundary(m_text, m_caretIndex), extendsSelection);
    }
}

void TextField::moveCaretRight(bool extendsSelection) {
    if (hasSelection() && !extendsSelection) {
        setCaretIndex(getSelectionEnd(), false);
    } else {
        setCaretIndex(nextUtf8Boundary(m_text, m_caretIndex), extendsSelection);
    }
}

void TextField::moveCaretToTextStart(bool extendsSelection) {
    setCaretIndex(0, extendsSelection);
}

void TextField::moveCaretToTextEnd(bool extendsSelection) {
    setCaretIndex(m_text.size(), extendsSelection);
}

void TextField::moveCaretToLineStart(bool extendsSelection) {
    std::shared_ptr<FontAtlas> font = resolveFont();
    if (!font) { return; }
    const TextLayoutResult &layout = cachedTextLayout(*font, textMaxWidth(getFrame().size));
    if (layout.lines.empty()) { return; }
    setCaretIndex(layout.lines[lineIndexForCaret(layout)].start, extendsSelection);
}

void TextField::moveCaretToLineEnd(bool extendsSelection) {
    std::shared_ptr<FontAtlas> font = resolveFont();
    if (!font) { return; }
    const TextLayoutResult &layout = cachedTextLayout(*font, textMaxWidth(getFrame().size));
    if (layout.lines.empty()) { return; }
    const TextLine &line = layout.lines[lineIndexForCaret(layout)];
    setCaretIndex(line.start + line.length, extendsSelection);
}

void TextField::moveCaretVertically(int direction, bool extendsSelection) {
    std::shared_ptr<FontAtlas> font = resolveFont();
    if (!font) { return; }

    const TextLayoutResult &layout = cachedTextLayout(*font, textMaxWidth(getFrame().size));
    if (layout.lines.empty()) { return; }
    const size_t currentLineIndex = lineIndexForCaret(layout);
    const int nextLineIndex =
        std::clamp(static_cast<int>(currentLineIndex) + direction, 0, static_cast<int>(layout.lines.size() - 1));
    if (nextLineIndex == static_cast<int>(currentLineIndex)) { return; }

    const TextLine &currentLine = layout.lines[currentLineIndex];
    const float caretX = caretXInLine(*font, currentLine, m_caretIndex);
    const TextLine &nextLine = layout.lines[static_cast<size_t>(nextLineIndex)];
    size_t nextCaretIndex = nextLine.start;

    const size_t nextLineEnd = nextLine.start + nextLine.length;
    float bestDistance = std::abs(caretX);
    float x = 0.f;
    for (size_t index = nextLine.start; index < nextLineEnd;) {
        const size_t next = nextUtf8Boundary(m_text, index);
        x += widthOfTextRange(*font, m_text, index, next, effectiveFontSize());
        const float distance = std::abs(x - caretX);
        if (distance < bestDistance) {
            bestDistance = distance;
            nextCaretIndex = next;
        }
        index = next;
    }
    setCaretIndex(nextCaretIndex, extendsSelection);
}

void TextField::setCaretIndex(size_t index, bool extendsSelection) {
    m_caretIndex = std::min(index, m_text.size());
    if (!extendsSelection) { m_selectionAnchorIndex = m_caretIndex; }
    resetCaretBlink();
    setNeedsDisplay();
}

void TextField::selectRange(SelectionRange range) {
    range.start = std::min(range.start, m_text.size());
    range.end = std::min(range.end, m_text.size());
    if (range.start > range.end) { std::swap(range.start, range.end); }

    m_selectionAnchorIndex = range.start;
    m_caretIndex = range.end;
    resetCaretBlink();
    setNeedsDisplay();
}

void TextField::extendSelectionToRange(SelectionRange range) {
    range.start = std::min(range.start, m_text.size());
    range.end = std::min(range.end, m_text.size());
    if (range.start > range.end) { std::swap(range.start, range.end); }

    if (range.start < m_pointerSelectionAnchorRange.start) {
        m_selectionAnchorIndex = m_pointerSelectionAnchorRange.end;
        m_caretIndex = range.start;
    } else {
        m_selectionAnchorIndex = m_pointerSelectionAnchorRange.start;
        m_caretIndex = range.end;
    }
    resetCaretBlink();
    setNeedsDisplay();
}

void TextField::placeCaret(Point windowPosition, bool extendsSelection) {
    Rect worldFrame = getWorldFrame();
    Point localContentPosition(
        windowPosition.x - worldFrame.origin.x - m_appearance.contentInsets.left + m_scrollOffsetX,
        windowPosition.y - worldFrame.origin.y - m_appearance.contentInsets.top
    );
    setCaretIndex(closestTextIndexAt(localContentPosition), extendsSelection);
}

TextField::SelectionRange TextField::selectionRangeAt(Point windowPosition, int clickCount) const {
    Rect worldFrame = getWorldFrame();
    Point localContentPosition(
        windowPosition.x - worldFrame.origin.x - m_appearance.contentInsets.left + m_scrollOffsetX,
        windowPosition.y - worldFrame.origin.y - m_appearance.contentInsets.top
    );
    const size_t textIndex = closestTextIndexAt(localContentPosition);
    if (clickCount <= 1) { return {textIndex, textIndex}; }
    if (clickCount == 2) { return wordRangeAt(textIndex); }
    return lineRangeAt(textIndex);
}

int TextField::nextClickCount(Point position) {
    const double timestamp = currentTimeSeconds();
    const bool continuesClickChain = timestamp - m_lastClickTimestamp <= CLICK_CHAIN_INTERVAL
        && squaredDistance(position, m_lastClickPosition) <= CLICK_CHAIN_DISTANCE * CLICK_CHAIN_DISTANCE;

    m_clickCount = continuesClickChain ? std::min(m_clickCount + 1, 3) : 1;
    m_lastClickTimestamp = timestamp;
    m_lastClickPosition = position;
    return m_clickCount;
}

bool TextField::isSelectionPointer(const PointerEvent &event) const {
    if (!m_isSelectingWithPointer || event.type != m_selectionPointerType) { return false; }
    return event.type == PointerType::Mouse || event.pointerId == m_selectionPointerId;
}

void TextField::drawSelection(const FontAtlas &font, const TextLayoutResult &layout, Point textOrigin) const {
    Context *context = getContext();
    if (!context) { return; }

    const size_t selectionStart = getSelectionStart();
    const size_t selectionEnd = getSelectionEnd();
    for (size_t lineIndex = 0; lineIndex < layout.lines.size(); ++lineIndex) {
        const TextLine &line = layout.lines[lineIndex];
        const size_t lineStart = line.start;
        const size_t lineEnd = line.start + line.length;
        const size_t start = std::max(selectionStart, lineStart);
        const size_t end = std::min(selectionEnd, lineEnd);
        if (start >= end) { continue; }

        const float x0 = caretXInLine(font, line, start);
        const float x1 = caretXInLine(font, line, end);
        const float width = std::max(1.f, x1 - x0);
        const float lineY = textOrigin.y + (layout.lineHeight + layout.lineSpacing) * static_cast<float>(lineIndex);

        UIRenderer::RectData rect;
        rect.center = {textOrigin.x + x0 + width * 0.5f, lineY + layout.lineHeight * 0.5f, 0.f};
        rect.size = {width, layout.lineHeight};
        rect.color = m_selectionColor;
        context->getRenderer().drawRect(rect);
    }
}

size_t TextField::closestTextIndexAt(Point localContentPosition) const {
    std::shared_ptr<FontAtlas> font = resolveFont();
    if (!font) { return m_caretIndex; }

    const Rect frame = getFrame();
    const TextLayoutResult &layout = cachedTextLayout(*font, textMaxWidth(frame.size));
    if (layout.lines.empty()) { return 0; }

    const float y = localContentPosition.y - textOriginY(frame.size, layout);
    const float lineStep = layout.lineHeight + layout.lineSpacing;
    const int lineIndex = std::clamp(
        static_cast<int>(std::floor(y / std::max(1.f, lineStep))), 0, static_cast<int>(layout.lines.size() - 1)
    );
    const TextLine &line = layout.lines[static_cast<size_t>(lineIndex)];

    // Account for the horizontal alignment: the position is relative to the actual text origin.
    const float localX = localContentPosition.x - textOriginX(layout, frame.size);
    const size_t lineEnd = line.start + line.length;
    size_t closestIndex = line.start;
    float bestDistance = std::abs(localX);
    float x = 0.f;
    for (size_t index = line.start; index < lineEnd;) {
        const size_t next = nextUtf8Boundary(m_text, index);
        x += widthOfTextRange(*font, m_text, index, next, effectiveFontSize());
        const float distance = std::abs(x - localX);
        if (distance < bestDistance) {
            bestDistance = distance;
            closestIndex = next;
        }
        index = next;
    }
    return closestIndex;
}

size_t TextField::lineIndexForCaret(const TextLayoutResult &layout) const {
    for (size_t lineIndex = 0; lineIndex < layout.lines.size(); ++lineIndex) {
        const TextLine &line = layout.lines[lineIndex];
        if (m_caretIndex >= line.start && m_caretIndex <= line.start + line.length) { return lineIndex; }
    }
    return layout.lines.empty() ? 0 : layout.lines.size() - 1;
}

size_t TextField::lineIndexForTextIndex(const TextLayoutResult &layout, size_t textIndex) const {
    textIndex = std::min(textIndex, m_text.size());
    for (size_t lineIndex = 0; lineIndex < layout.lines.size(); ++lineIndex) {
        const TextLine &line = layout.lines[lineIndex];
        if (textIndex >= line.start && textIndex <= line.start + line.length) { return lineIndex; }
    }
    return layout.lines.empty() ? 0 : layout.lines.size() - 1;
}

TextField::SelectionRange TextField::wordRangeAt(size_t textIndex) const {
    if (m_text.empty()) { return {}; }

    textIndex = std::min(textIndex, m_text.size());
    if (textIndex == m_text.size()) {
        textIndex = previousUtf8Boundary(m_text, textIndex);
    }

    const CharacterClass selectedClass = characterClassAt(m_text, textIndex);
    size_t start = textIndex;
    while (start > 0) {
        const size_t previous = previousUtf8Boundary(m_text, start);
        if (characterClassAt(m_text, previous) != selectedClass) { break; }
        start = previous;
    }

    size_t end = nextUtf8Boundary(m_text, textIndex);
    while (end < m_text.size() && characterClassAt(m_text, end) == selectedClass) {
        end = nextUtf8Boundary(m_text, end);
    }

    return {start, end};
}

TextField::SelectionRange TextField::lineRangeAt(size_t textIndex) const {
    if (m_text.empty()) { return {}; }

    std::shared_ptr<FontAtlas> font = resolveFont();
    if (!font) { return {0, m_text.size()}; }

    const TextLayoutResult &layout = cachedTextLayout(*font, textMaxWidth(getFrame().size));
    if (layout.lines.empty()) { return {0, m_text.size()}; }

    const TextLine &line = layout.lines[lineIndexForTextIndex(layout, textIndex)];
    return {line.start, line.start + line.length};
}

float TextField::caretXInLine(const FontAtlas &font, const TextLine &line, size_t caretIndex) const {
    caretIndex = std::clamp(caretIndex, line.start, line.start + line.length);
    return widthOfTextRange(font, m_text, line.start, caretIndex, effectiveFontSize());
}

float TextField::contentWidth(Size frameSize) const {
    return std::max(0.f, frameSize.width - m_appearance.contentInsets.left - m_appearance.contentInsets.right);
}

float TextField::textMaxWidth(Size frameSize) const {
    return isMultiline() ? contentWidth(frameSize) : std::numeric_limits<float>::infinity();
}

float TextField::textOriginY(Size frameSize, const TextLayoutResult &layout) const {
    if (isMultiline()) { return 0.f; }
    const float contentHeight = std::max(0.f, frameSize.height - m_appearance.contentInsets.top - m_appearance.contentInsets.bottom);
    return std::max(0.f, (contentHeight - layout.size.height) * 0.5f);
}

float TextField::textOriginX(const TextLayoutResult &layout, Size frameSize) const {
    // Horizontal alignment offset for single-line when the text fits into the visible width.
    // On overflow — regular left alignment + horizontal scroll (offset 0).
    if (m_alignment == TextAlignment::Left || isMultiline() || layout.lines.empty()) { return 0.f; }
    const float visibleWidth = contentWidth(frameSize);
    const float lineWidth = layout.lines.front().size.width;
    if (lineWidth >= visibleWidth) { return 0.f; }
    const float freeWidth = visibleWidth - lineWidth;
    return m_alignment == TextAlignment::Center ? freeWidth * 0.5f : freeWidth;
}

float TextField::clampedScrollOffsetX(const FontAtlas &font, const TextLayoutResult &layout, Size frameSize) const {
    // Horizontal scroll only for single-line; multiline wraps/grows in height.
    if (isMultiline() || layout.lines.empty()) { return 0.f; }
    const float visibleWidth = contentWidth(frameSize);
    if (visibleWidth <= 0.f) { return 0.f; }

    const TextLine &line = layout.lines.front();
    const float caretX = caretXInLine(font, line, m_caretIndex);

    // The window is shifted just enough for the caret to stay within [offset, offset+width].
    float offset = m_scrollOffsetX;
    if (caretX < offset) {
        offset = caretX;
    } else if (caretX > offset + visibleWidth) {
        offset = caretX - visibleWidth;
    }
    const float maxOffset = std::max(0.f, line.size.width - visibleWidth);
    return std::clamp(offset, 0.f, maxOffset);
}

} // namespace PandaUI
