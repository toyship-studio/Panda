#pragma once

#include "PandaUI/Core/Appearance.hpp"
#include "PandaUI/Elements/Control.hpp"
#include "PandaUI/Rendering/Font.hpp" // FontAtlas (resource) + Font (descriptor)

#include <functional>
#include <string>

namespace PandaUI {

// The look of a text field: the recessed plate per state (only the border reacts in the editor),
// the colors of the text, the caret and the selection, the font, the insets and the height.
struct TextFieldAppearance {
    SurfaceStyle normal{ThemeColor::input, ThemeColor::borderMed};
    SurfaceStyle hovered{ThemeColor::input, ThemeColor::borderStrong};
    SurfaceStyle focused{ThemeColor::input, ThemeColor::focusRing};
    SurfaceStyle disabled{ThemeColor::inputDeep, ThemeColor::borderMed};
    ThemeColor text = ThemeColor::textPrimary;
    ThemeColor placeholder = ThemeColor::textDim;
    ThemeColor caret = ThemeColor::focusRing;
    ThemeColor selection = ThemeColor::textSelection;
    Font font = Font(16.f);
    // Vertical insets small → the floor (minHeight) sets the height; the text centers itself.
    EdgeInsets contentInsets = EdgeInsets(4.f, Spacing::md, Spacing::md, 4.f);
    float minHeight = 28.f;
    CornerRadii cornerRadius = CornerRadii(Radius::sm); // the radius of every input (Combobox, Vector3, ColorWell)
    float borderWidth = 1.f;
    float caretWidth = 1.5f;
};

class TextField : public Control {
public:
    using TextChangedCallback = std::function<void(TextField &)>;
    using SubmitCallback = std::function<void(TextField &)>;

    TextField();
    explicit TextField(std::string text);

    // The look of the fields created from now on (Appearance.hpp).
    static TextFieldAppearance &defaultAppearance();
    void setAppearance(const TextFieldAppearance &appearance);
    const TextFieldAppearance &appearance() const {
        return m_appearance;
    }

    // measure-leaf (intrinsic size → Yoga measure function): it must not have children — forbidden at
    // compile time (otherwise Yoga crashes at runtime). Accessories/icons go into a wrapper container.
    void addSubview(std::shared_ptr<View>) = delete;

    void setText(std::string text);
    const std::string &getText() const {
        return m_text;
    }
    bool hasSelection() const;
    size_t getSelectionStart() const;
    size_t getSelectionEnd() const;
    void selectAll();

    void setPlaceholder(std::string placeholder);
    const std::string &getPlaceholder() const {
        return m_placeholder;
    }

    void setFont(Font font); // family + weight + point size in a single setter (UIKit-style)
    const Font &getFont() const {
        return m_appearance.font;
    }
    void setNumberOfLines(int numberOfLines);
    int getNumberOfLines() const {
        return m_numberOfLines;
    }
    bool isMultiline() const {
        return m_numberOfLines != 1;
    }
    bool isTextInput() const override {
        return true;
    }
    void setLineSpacing(float lineSpacing);
    void setContentInsets(EdgeInsets insets);
    void setTextAlignment(TextAlignment alignment); // single-line: horizontal text alignment

    void setOnTextChanged(TextChangedCallback callback);
    void setOnSubmit(SubmitCallback callback);

    void update(double deltaTime) override;
    bool pointerDown(PointerEvent &event) override;
    bool pointerUp(PointerEvent &event) override;
    bool pointerMove(PointerEvent &event) override;
    bool keyDown(KeyEvent &event) override;
    bool textInput(TextInputEvent &event) override;
    void interactionCancelled() override;

protected:
    Size intrinsicContentSize(Size availableSize) const override;
    void draw(float offsetX, float offsetY) override;
    void controlStateChanged(ControlState previousState, ControlState currentState) override;

private:
    struct SelectionRange {
        size_t start = 0;
        size_t end = 0;
    };

    TextLayoutOptions makeLayoutOptions(float maxWidth) const;
    TextLayoutResult layoutText(const FontAtlas &font, std::string_view text, float maxWidth) const;
    const TextLayoutResult &cachedTextLayout(const FontAtlas &font, float maxWidth) const;
    void invalidateTextLayout();
    void applyAppearance(); // geometry and font; then the colors
    void updateAppearance();
    void applyThemeColors();
    void themeChanged() override;
    void textDidChange();
    void submit();
    void insertText(std::string_view text);
    void replaceSelectionWith(std::string_view text);
    void copySelection() const;
    void cutSelection();
    void pasteClipboard();
    std::string sanitizePastedText(std::string_view text) const;
    void resetCaretBlink();
    void deleteBackward();
    void deleteForward();
    void moveCaretLeft(bool extendsSelection);
    void moveCaretRight(bool extendsSelection);
    void moveCaretToTextStart(bool extendsSelection);
    void moveCaretToTextEnd(bool extendsSelection);
    void moveCaretToLineStart(bool extendsSelection);
    void moveCaretToLineEnd(bool extendsSelection);
    void moveCaretVertically(int direction, bool extendsSelection);
    void setCaretIndex(size_t index, bool extendsSelection);
    void selectRange(SelectionRange range);
    void extendSelectionToRange(SelectionRange range);
    void placeCaret(Point windowPosition, bool extendsSelection);
    SelectionRange selectionRangeAt(Point windowPosition, int clickCount) const;
    int nextClickCount(Point position);
    bool isSelectionPointer(const PointerEvent &event) const;
    void drawSelection(const FontAtlas &font, const TextLayoutResult &layout, Point textOrigin) const;
    size_t closestTextIndexAt(Point localContentPosition) const;
    size_t lineIndexForCaret(const TextLayoutResult &layout) const;
    size_t lineIndexForTextIndex(const TextLayoutResult &layout, size_t textIndex) const;
    SelectionRange wordRangeAt(size_t textIndex) const;
    SelectionRange lineRangeAt(size_t textIndex) const;
    float caretXInLine(const FontAtlas &font, const TextLine &line, size_t caretIndex) const;
    float contentWidth(Size frameSize) const;
    float textMaxWidth(Size frameSize) const;
    float textOriginY(Size frameSize, const TextLayoutResult &layout) const;
    float textOriginX(const TextLayoutResult &layout, Size frameSize) const; // alignment offset (single-line)
    float clampedScrollOffsetX(const FontAtlas &font, const TextLayoutResult &layout, Size frameSize) const;
    std::shared_ptr<FontAtlas> resolveFont() const; // active font of the field (takes FontFamily into account)
    float effectiveFontSize() const;                // the point size times the text scale of the context

    std::string m_text;
    std::string m_placeholder;
    TextChangedCallback m_onTextChanged;
    SubmitCallback m_onSubmit;
    TextFieldAppearance m_appearance;
    Color m_textColor; // the colors of the appearance, resolved (applyThemeColors)
    Color m_placeholderColor;
    Color m_caretColor;
    Color m_selectionColor;
    int m_numberOfLines;
    float m_lineSpacing;
    TextAlignment m_alignment = TextAlignment::Left;
    size_t m_caretIndex;
    size_t m_selectionAnchorIndex;
    double m_caretBlinkElapsed;
    bool m_caretVisible;
    bool m_isSelectingWithPointer;
    PointerType m_selectionPointerType;
    int m_selectionPointerId;
    int m_clickCount;
    int m_selectionClickCount;
    double m_lastClickTimestamp;
    Point m_lastClickPosition;
    SelectionRange m_pointerSelectionAnchorRange;

    // Horizontal text offset for single-line: the caret is kept within the visible area, a long line
    // scrolls horizontally. Recomputed in draw, read during hit testing. Always 0 for multiline.
    float m_scrollOffsetX = 0.f;

    // Layout cache of m_text: reused in draw (body/selection/caret), navigation and hit testing.
    // Validity is reset when the text/options change; the width and font are checked on access.
    mutable TextLayoutResult m_cachedLayout;
    mutable const FontAtlas *m_cachedLayoutFont = nullptr;
    mutable float m_cachedLayoutMaxWidth = 0.f;
    mutable bool m_layoutCacheValid = false;
};

} // namespace PandaUI
