#include "PandaUI/Elements/CollapsibleSection.hpp"

#include "PandaUI/Core/Theme.hpp"
#include "PandaUI/Elements/Control.hpp"
#include "PandaUI/Elements/Label.hpp"
#include "PandaUI/Rendering/UIRenderer.hpp"
#include "PandaUI/Runtime/Context.hpp"

#include <utility>

namespace PandaUI {

// Group header: a clickable Control with a chevron arrow (drawn) and a caption.
class CollapsibleSection::Header : public Control {
public:
    Header(CollapsibleSection *owner, std::string title)
        : m_owner(owner) {
        layout().setFlexDirection(FlexDirection::Row);
        layout().setAlignItems(Align::Center);

        m_title = std::make_shared<Label>(std::move(title));
        addSubview(m_title);
        applyAppearance();
    }

    // The geometry and the title font of the appearance; the explicit setters of the section win.
    void applyAppearance() {
        const CollapsibleSectionAppearance &a = m_owner->appearance();
        layout().setHeight(Length::points(a.headerHeight));
        layout().setPadding(Edge::Left, a.headerPadLeft + a.disclosureSize + a.labelGap);
        layout().setPadding(Edge::Right, a.headerPadRight);
        surface().setCornerRadius(a.headerRadius);
        if (!m_hasTitleFont) { m_title->setFont(a.titleFont); }
        applyStyle();
        setNeedsDisplay();
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
        m_hasTitleFont = true;
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

        const CollapsibleSectionAppearance &a = m_owner->appearance();
        const Rect frame = getFrame();
        const float cx = offsetX + frame.origin.x + a.headerPadLeft + a.disclosureSize * 0.5f;
        const float cy = offsetY + frame.origin.y + frame.size.height * 0.5f;
        const Color color = a.chevron.resolve();
        const float lng = a.chevronLong;
        const float sht = a.chevronShort;
        const float thick = a.chevronThickness;

        if (m_expanded) {
            renderer.drawLine(Point(cx - lng, cy - sht), Point(cx, cy + sht), thick, color);
            renderer.drawLine(Point(cx, cy + sht), Point(cx + lng, cy - sht), thick, color);
        } else {
            renderer.drawLine(Point(cx - sht, cy - lng), Point(cx + sht, cy), thick, color);
            renderer.drawLine(Point(cx + sht, cy), Point(cx - sht, cy + lng), thick, color);
        }
    }

private:
    void applyStyle() {
        const CollapsibleSectionAppearance &a = m_owner->appearance();
        if (!m_headerFill) {
            setBackgroundColor(Color(0x00000000)); // flat header (inspector components): no backing
        } else if (hasControlState(ControlState::Focused)) {
            setBackgroundColor(a.headerFocused);
        } else if (hasControlState(ControlState::Hovered) || hasControlState(ControlState::Highlighted)) {
            setBackgroundColor(a.headerHovered);
        } else {
            setBackgroundColor(a.headerBackground);
        }
        if (m_headerFill && a.headerImage) {
            surface().setBackgroundImage(a.headerImage);
        } else {
            surface().clearBackgroundImage();
        }
        if (!m_hasTitleColor) { m_title->setTextColor(a.titleColor); }
    }

    CollapsibleSection *m_owner;
    std::shared_ptr<Label> m_title;
    bool m_expanded = true;
    bool m_headerFill = true;
    bool m_hasTitleColor = false;
    bool m_hasTitleFont = false;
};

CollapsibleSection::CollapsibleSection()
    : CollapsibleSection(std::string()) {}

CollapsibleSectionAppearance &CollapsibleSection::defaultAppearance() {
    static CollapsibleSectionAppearance appearance;
    return appearance;
}

CollapsibleSection::CollapsibleSection(std::string title)
    : m_appearance(defaultAppearance()) {
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
    m_clip->addSubview(m_body);
    applyAppearance();
}

CollapsibleSection::~CollapsibleSection() = default;

void CollapsibleSection::setAppearance(const CollapsibleSectionAppearance &appearance) {
    m_appearance = appearance;
    applyAppearance();
}

void CollapsibleSection::applyAppearance() {
    const CollapsibleSectionAppearance &a = m_appearance;
    m_body->layout().setGap(a.bodyGap);
    m_body->layout().setPadding(Edge::Top, a.bodyPadding.top); // the gap between the header and the content
    m_body->layout().setPadding(Edge::Left, a.bodyPadding.left);
    m_body->layout().setPadding(Edge::Right, a.bodyPadding.right);
    m_body->layout().setPadding(Edge::Bottom, a.bodyPadding.bottom);
    m_header->applyAppearance();
}

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
