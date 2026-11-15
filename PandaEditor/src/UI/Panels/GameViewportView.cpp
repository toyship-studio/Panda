#include "UI/Panels/GameViewportView.hpp"

#include <PandaEditorKit/Widgets/Typography.hpp>

namespace Editor {

using namespace PandaUI;

GameViewportView::GameViewportView() {
    setFocusable(true); // a click → the focus → the keys of the game
    setBackgroundColor(ThemeColor::windowBackground);
    layout().setFlexGrow(1.f);
    layout().setFlexDirection(FlexDirection::Column);
    layout().setJustifyContent(Justify::Center);
    layout().setAlignItems(Align::Center);

    // The frame fills the tab 1:1 (the surface is rendered at the panel size), does not intercept clicks.
    m_image = std::make_shared<ImageView>();
    m_image->setUserInteractionEnabled(false);
    m_image->setContentMode(ImageContentMode::Stretch);
    m_image->layout().setPositionType(PositionType::Absolute);
    m_image->layout().setPosition(Edge::Left, Length::points(0.f));
    m_image->layout().setPosition(Edge::Top, Length::points(0.f));
    m_image->layout().setPosition(Edge::Right, Length::points(0.f));
    m_image->layout().setPosition(Edge::Bottom, Length::points(0.f));
    addSubview(m_image);

    m_message = std::make_shared<Label>("Game");
    m_message->setFont(Typo::Caption);
    m_message->setTextRole(TextRole::Dim);
    m_message->setUserInteractionEnabled(false);
    addSubview(m_message);

    m_veil = std::make_shared<ViewportVeil>("Not rendering: the Scene has the focus");
    addSubview(m_veil);
}

void GameViewportView::setTexture(TextureHandle texture, bool flipVertical) {
    m_image->setTexture(texture);
    m_image->setFlipVertical(flipVertical);
    m_image->setHidden(!texture.isValid());
}

void GameViewportView::setMessage(const std::string &text) {
    const bool shown = !text.empty();
    if (shown) { m_message->setText(text); }
    m_message->setHidden(!shown);
    m_message->layout().setDisplay(shown ? Display::Flex : Display::None);
}

bool GameViewportView::pointerDown(PointerEvent &) {
    if (Context *context = getContext()) { context->setFocusedView(this); }
    return false; // not consumed: the click belongs to the game (Panda::Input)
}

} // namespace Editor
