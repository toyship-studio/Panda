#include "PandaUI/Elements/Dialog.hpp"

#include "PandaUI/Core/KeyboardEvent.hpp"
#include "PandaUI/Core/Theme.hpp"

#include <utility>

namespace PandaUI {

namespace {

    std::shared_ptr<Panel> makeCard() {
        auto card = std::make_shared<Panel>();
        applyElevation(*card, Elevation::Modal);
        card->layout().setFlexDirection(FlexDirection::Column);
        return card;
    }

} // namespace

DialogAppearance &Dialog::defaultAppearance() {
    static DialogAppearance appearance;
    return appearance;
}

Dialog::Dialog(std::string title)
    : Modal(makeCard())
    , m_appearance(defaultAppearance())
    , m_card(std::static_pointer_cast<Panel>(getContent())) {
    m_titleLabel = std::make_shared<Label>(std::move(title));
    m_card->addSubview(m_titleLabel);

    m_messageLabel = std::make_shared<Label>();
    m_messageLabel->setNumberOfLines(0);
    m_messageLabel->setHidden(true);
    m_card->addSubview(m_messageLabel);

    m_bodyHolder = std::make_shared<Panel>();
    m_bodyHolder->setBackgroundColor(Color(0x00000000));
    m_bodyHolder->layout().setFlexDirection(FlexDirection::Column);
    m_bodyHolder->layout().setFlexShrink(1.f);
    m_bodyHolder->setHidden(true);
    m_card->addSubview(m_bodyHolder);

    m_actionRow = std::make_shared<Panel>();
    m_actionRow->setBackgroundColor(Color(0x00000000));
    m_actionRow->layout().setFlexDirection(FlexDirection::Row);
    m_actionRow->layout().setJustifyContent(Justify::End);
    m_card->addSubview(m_actionRow);

    applyAppearance();
}

void Dialog::setAppearance(const DialogAppearance &appearance) {
    m_appearance = appearance;
    applyAppearance();
}

void Dialog::applyAppearance() {
    const DialogAppearance &a = m_appearance;
    a.card.applyTo(m_card->surface());
    m_card->layout().setPadding(a.padding);
    m_card->layout().setGap(a.gap);
    m_card->layout().setWidth(Length::points(a.width));
    m_titleLabel->setFont(a.titleFont);
    m_titleLabel->setTextColor(a.titleColor);
    m_messageLabel->setFont(a.messageFont);
    m_messageLabel->setTextColor(a.messageColor);
    m_actionRow->layout().setGap(a.actionGap);
    m_actionRow->layout().setMargin(Edge::Top, a.actionsTopMargin);
}

void Dialog::setMessage(std::string message) {
    m_messageLabel->setHidden(message.empty());
    m_messageLabel->setText(std::move(message));
}

void Dialog::setBody(std::shared_ptr<View> body) {
    m_bodyHolder->removeAllSubviews();
    m_bodyHolder->setHidden(body == nullptr);
    if (body) { m_bodyHolder->addSubview(std::move(body)); }
}

void Dialog::setContentWidth(float width) {
    m_appearance.width = width;
    m_card->layout().setWidth(Length::points(width));
}

std::shared_ptr<Button>
Dialog::addAction(std::string title, DialogActionRole role, std::function<void()> handler) {
    auto button = std::make_shared<Button>(std::move(title));
    if (role == DialogActionRole::Primary) { button->setStyle(ButtonStyle::Accent); }
    if (role == DialogActionRole::Destructive) { button->setStyle(ButtonStyle::Destructive); }
    // handler — BEFORE dismiss: the overlay removal is deferred, the dialog form is still alive and readable.
    std::function<void()> run = [this, handler = std::move(handler)] {
        if (handler) { handler(); }
        dismiss();
    };
    button->setOnClick([run](Button &) { run(); });
    if (role == DialogActionRole::Primary || role == DialogActionRole::Destructive) {
        m_enterButton = button;
        m_enterAction = std::move(run);
    }
    m_actionRow->addSubview(button);
    return button;
}

bool Dialog::keyDown(KeyEvent &event) {
    // Enter that was not consumed by an input field (NumberField/TextField commit on it themselves) is
    // the primary action; a disabled button (the form is not valid) does not accept it.
    const bool enter = event.key == Key::Enter || event.key == Key::KeypadEnter;
    if (enter && m_enterButton && m_enterButton->isEnabled()) {
        m_enterAction();
        return true;
    }
    return Modal::keyDown(event);
}

} // namespace PandaUI
