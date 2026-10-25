#include "PandaUI/Elements/CollapsibleSection.hpp"

#include "PandaUI/Core/Theme.hpp"
#include "PandaUI/Elements/Control.hpp"
#include "PandaUI/Elements/Label.hpp"
#include "PandaUI/Rendering/UIRenderer.hpp"
#include "PandaUI/Runtime/Context.hpp"

#include <utility>

namespace PandaUI {

namespace {
    constexpr float HEADER_HEIGHT = 34.f;
    constexpr float HEADER_PAD_LEFT = 8.f;
    constexpr float DISCLOSURE_SIZE = 14.f;
    constexpr float LABEL_GAP = 4.f;
    constexpr float CHEVRON_LONG = 3.6f;
    constexpr float CHEVRON_SHORT = 1.9f;
    constexpr float CHEVRON_THICKNESS = 1.6f;
}

// Group header: a clickable Control with a chevron arrow (drawn) and a caption.
class CollapsibleSection::Header : public Control {
public:
    Header(CollapsibleSection *owner, std::string title)
        : m_owner(owner) {
        layout().setFlexDirection(FlexDirection::Row);
        layout().setAlignItems(Align::Center);
        layout().setHeight(Length::points(HEADER_HEIGHT));
        layout().setPadding(Edge::Left, HEADER_PAD_LEFT + DISCLOSURE_SIZE + LABEL_GAP);
        layout().setPadding(Edge::Right, 8.f);
        surface().setCornerRadius(6.f);

        m_title = std::make_shared<Label>(std::move(title));
        m_title->setFont(Font(18.f, FontWeight::Bold)); // component header: larger than the body, bold
        addSubview(m_title);
        applyStyle();
    }

    void setTitle(std::string title) {
        m_title->setText(std::move(title));
    }
    void setExpandedVisual(bool expanded) {
        if (m_expanded == expanded) { return; }
        m_expanded = expanded;
        setNeedsDisplay();
    }
    void setTitleFont(Font font) {
        m_title->setFont(font);
    }
    void setTitleColor(ThemeColor color) {
        m_hasTitleColor = true;
        m_title->setTextColor(color);
    }
    void setTitleLetterSpacing(float spacing) {
        m_title->setLetterSpacing(spacing);
    }
    void setHeaderFill(bool fill) {
        m_headerFill = fill;
        applyStyle();
    }
    void setAccessory(std::shared_ptr<View> view) {
        // Trailing slot: a spacer pushes the accessory to the right edge of the header (on top of the toggle
        // click — the child control catches the click itself and does not let it reach the section toggle).
        auto spacer = std::make_shared<Panel>();
        spacer->setBackgroundColor(Color(0x00000000));
        spacer->layout().setFlexGrow(1.f);
        addSubview(spacer);
        addSubview(std::move(view));
    }

protected:
    void performPrimaryAction() override {
        m_owner->toggle();
    }
    void controlStateChanged(ControlState, ControlState) override {
        applyStyle();
    }
    void themeChanged() override {
        applyStyle();
        setNeedsDisplay();
    }
    void draw(float offsetX, float offsetY) override {
        Context *context = getContext();
        if (!context) { return; }
        UIRenderer &renderer = context->getRenderer();

        const Rect frame = getFrame();
        const float cx = offsetX + frame.origin.x + HEADER_PAD_LEFT + DISCLOSURE_SIZE * 0.5f;
        const float cy = offsetY + frame.origin.y + frame.size.height * 0.5f;
        const Color color = theme().textDim;

        if (m_expanded) {
            renderer.drawLine(
                Point(cx - CHEVRON_LONG, cy - CHEVRON_SHORT), Point(cx, cy + CHEVRON_SHORT), CHEVRON_THICKNESS, color
            );
            renderer.drawLine(
                Point(cx, cy + CHEVRON_SHORT), Point(cx + CHEVRON_LONG, cy - CHEVRON_SHORT), CHEVRON_THICKNESS, color
            );
        } else {
            renderer.drawLine(
                Point(cx - CHEVRON_SHORT, cy - CHEVRON_LONG), Point(cx + CHEVRON_SHORT, cy), CHEVRON_THICKNESS, color
            );
            renderer.drawLine(
                Point(cx + CHEVRON_SHORT, cy), Point(cx - CHEVRON_SHORT, cy + CHEVRON_LONG), CHEVRON_THICKNESS, color
            );
        }
    }

private:
    void applyStyle() {
        const Theme &t = theme();
        if (!m_headerFill) {
            setBackgroundColor(Color(0x00000000)); // flat header (inspector components): no backing
        } else if (hasControlState(ControlState::Focused)) {
            setBackgroundColor(t.selection); // soft focus background (like the selection in collections)
        } else if (hasControlState(ControlState::Hovered) || hasControlState(ControlState::Highlighted)) {
            setBackgroundColor(t.controlHover);
        } else {
            setBackgroundColor(t.surface); // slightly different background separates the header from the content
        }
        if (!m_hasTitleColor) { m_title->setTextRole(TextRole::Primary); }
    }

    CollapsibleSection *m_owner;
    std::shared_ptr<Label> m_title;
    bool m_expanded = true;
    bool m_headerFill = true;
    bool m_hasTitleColor = false;
};

CollapsibleSection::CollapsibleSection()
    : CollapsibleSection(std::string()) {}

CollapsibleSection::CollapsibleSection(std::string title) {
    m_title = title;
    setBackgroundColor(Color(0x00000000));
    layout().setFlexDirection(FlexDirection::Column);

    m_header = std::make_shared<Header>(this, std::move(title));
    addSubview(m_header);

    m_clip = std::make_shared<View>();
    m_clip->setBackgroundColor(Color(0x00000000));
    m_clip->setClipsToBounds(true);
    m_clip->layout().setWidth(Length::percent(100.f));
    addSubview(m_clip);

    m_body = std::make_shared<Panel>();
    m_body->setBackgroundColor(Color(0x00000000));
    m_body->layout().setFlexDirection(FlexDirection::Column);
    m_body->layout().setFlexShrink(0.f); // do not shrink while the clip height is animated
    m_body->layout().setWidth(Length::percent(100.f));
    m_body->layout().setGap(6.f);
    m_body->layout().setPadding(Edge::Horizontal, 8.f);
    m_body->layout().setPadding(Edge::Top, 6.f); // gap between the header and the content
    m_body->layout().setPadding(Edge::Bottom, 8.f);
    m_clip->addSubview(m_body);
}

CollapsibleSection::~CollapsibleSection() = default;

void CollapsibleSection::setTitle(std::string title) {
    m_title = title;
    m_header->setTitle(std::move(title));
}

void CollapsibleSection::setTitleFont(Font font) {
    m_header->setTitleFont(font);
}

void CollapsibleSection::setTitleColor(ThemeColor color) {
    m_header->setTitleColor(color);
}

void CollapsibleSection::setTitleLetterSpacing(float spacing) {
    m_header->setTitleLetterSpacing(spacing);
}

void CollapsibleSection::setHeaderFill(bool fill) {
    m_header->setHeaderFill(fill);
}

void CollapsibleSection::setHeaderAccessory(std::shared_ptr<View> view) {
    m_header->setAccessory(std::move(view));
}

void CollapsibleSection::addContent(std::shared_ptr<View> view) {
    m_body->addSubview(std::move(view));
}

void CollapsibleSection::setExpanded(bool expanded) {
    if (m_expanded == expanded) { return; }
    m_expanded = expanded;
    m_header->setExpandedVisual(expanded);
    // Instant, no animation: expanded → auto height (set by the body itself), collapsed → 0 (the clip hides it).
    m_clip->layout().setHeight(expanded ? Length::autoValue() : Length::points(0.f));
    if (m_onToggled) { m_onToggled(*this); }
}

} // namespace PandaUI
