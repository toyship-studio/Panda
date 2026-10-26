#pragma once

#include "PandaUI/Rendering/Font.hpp" // FontAtlas (resource) + Font (descriptor); pulls in TextLayout.hpp
#include "PandaUI/Core/View.hpp"

#include <string>

namespace PandaUI {

// Semantic role of the text. The color is taken from the theme by role and is updated when the
// theme changes, unless an explicit color is set via setTextColor.
enum class TextRole {
    Primary,   // main text, values
    Secondary, // regular rows, readable secondary content
    Muted,     // field labels, counters, glyphs at rest
    Dim,       // captions, placeholders, metadata
    Disabled,  // inactive
    Accent,    // accent text
};

class Label : public View {
public:
    Label();
    explicit Label(std::string text);

    // measure-leaf (intrinsic size → Yoga measure function): children are not allowed — forbidden
    // at compile time (otherwise Yoga crashes at runtime). Accessories/icons go into a wrapper
    // container.
    void addSubview(std::shared_ptr<View>) = delete;

    void setText(std::string text);
    const std::string &getText() const {
        return m_text;
    }
    void setTextColor(ThemeColor color); // a token follows the theme; a role is a token too
    void setTextRole(TextRole role);
    TextRole getTextRole() const {
        return m_textRole;
    }
    void setFont(Font font); // family + style + size in a single setter (UIKit-style)
    const Font &getFont() const {
        return m_font;
    }
    void setNumberOfLines(int numberOfLines);
    int getNumberOfLines() const {
        return m_numberOfLines;
    }
    void setTextAlignment(TextAlignment alignment);
    TextAlignment getTextAlignment() const {
        return m_textAlignment;
    }
    void setVerticalTextAlignment(TextVerticalAlignment alignment);
    TextVerticalAlignment getVerticalTextAlignment() const {
        return m_verticalTextAlignment;
    }
    void setLineSpacing(float lineSpacing);
    float getLineSpacing() const {
        return m_lineSpacing;
    }
    void setLetterSpacing(float letterSpacing); // tracking in px (uppercase panel/section headings)
    float getLetterSpacing() const {
        return m_letterSpacing;
    }
    void setTextOverflowMode(TextOverflowMode overflowMode);
    TextOverflowMode getTextOverflowMode() const {
        return m_textOverflowMode;
    }
    Size measureText() const;

protected:
    Size intrinsicContentSize(Size availableSize) const override;
    void draw(float offsetX, float offsetY) override;

private:
    TextLayoutOptions makeLayoutOptions(float maxWidth) const;
    const TextLayoutResult &getLayout(const FontAtlas &font, const TextLayoutOptions &options) const;
    void invalidateLayoutCache();
    void themeChanged() override;
    float effectiveFontSize() const; // the point size times the text scale of the context
    static ThemeColor roleColor(TextRole role);

    std::string m_text;
    ThemeColor m_textColor;
    TextRole m_textRole;
    Font m_font;
    int m_numberOfLines;
    float m_lineSpacing;
    float m_letterSpacing = 0.f;
    TextAlignment m_textAlignment;
    TextVerticalAlignment m_verticalTextAlignment;
    TextOverflowMode m_textOverflowMode;
    mutable const FontAtlas *m_cachedFont;
    mutable TextLayoutOptions m_cachedLayoutOptions;
    mutable TextLayoutResult m_cachedLayout;
    mutable bool m_cachedLayoutValid;
};

} // namespace PandaUI
