#include "PandaUI/Elements/Dialog.hpp"

#include "PandaUI/Core/KeyboardEvent.hpp"
#include "PandaUI/Core/Theme.hpp"

#include <utility>

namespace PandaUI {

namespace {

    constexpr float DEFAULT_WIDTH = 420.f;

    std::shared_ptr<Panel> makeCard() {
        auto card = std::make_shared<Panel>();
        card->setBackgroundColor(ThemeColor::surface);
        card->surface().setCornerRadius(10.f);
        card->surface().setBorderWidth(1.f);
        card->surface().setBorderColor(ThemeColor::borderMed);
        card->surface().setShadowColor(Color(0.f, 0.f, 0.f, 0.45f));
        card->surface().setShadowOffset({0.f, 12.f});
        card->surface().setShadowRadius(32.f);
        card->layout().setFlexDirection(FlexDirection::Column);
        card->layout().setPadding(20.f);
        card->layout().setGap(14.f);
        card->layout().setWidth(Length::points(DEFAULT_WIDTH));
        return card;
    }

} // namespace

Dialog::Dialog(std::string title)
    : Modal(makeCard())
    , m_card(std::static_pointer_cast<Panel>(getContent())) {
    m_titleLabel = std::make_shared<Label>(std::move(title));
    m_titleLabel->setFont(Font(18.f, FontWeight::Bold));
    m_card->addSubview(m_titleLabel);

    m_messageLabel = std::make_shared<Label>();
    m_messageLabel->setFont(Font(15.f));
    m_messageLabel->setTextRole(TextRole::Secondary);
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
    m_actionRow->layout().setGap(8.f);
    m_actionRow->layout().setMargin(Edge::Top, 4.f);
    m_card->addSubview(m_actionRow);
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
