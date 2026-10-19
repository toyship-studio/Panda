#include "UI/Panels/ViewportView.hpp"

#include <PandaEditorKit/Widgets/Typography.hpp>

#include <algorithm>
#include <cmath>

namespace Editor {

using namespace PandaUI;

namespace {
    constexpr float CLICK_SLOP = 4.f; // offset (px) beyond which an LMB press is a drag, not a click pick
} // namespace

ViewportView::ViewportView(ViewportViewOutput *output)
    : m_output(output) {
    setFocusable(true); // focus on click → the 1/2/3 keys work
    setBackgroundColor(theme().windowBackground);
    layout().setFlexGrow(1.f);
    layout().setFlexDirection(FlexDirection::Column);
    layout().setJustifyContent(Justify::Center);
    layout().setAlignItems(Align::Center);

    // Scene surface: on migration the engine's FBO texture is fed here (seam #1). Absolutely
    // stretched, does not intercept clicks (ViewportView itself catches them for picking).
    m_sceneImage = std::make_shared<ImageView>();
    m_sceneImage->setUserInteractionEnabled(false);
    // The FBO is rendered exactly at the panel size/aspect (syncExternalFrame) → Stretch = 1:1,
    // independent of the texture size (frozen at wrap). Fill (aspect-fill) would distort here when the
    // panel is resized.
    m_sceneImage->setContentMode(ImageContentMode::Stretch);
    m_sceneImage->layout().setPositionType(PositionType::Absolute);
    m_sceneImage->layout().setPosition(Edge::Left, Length::points(0.f));
    m_sceneImage->layout().setPosition(Edge::Top, Length::points(0.f));
    m_sceneImage->layout().setPosition(Edge::Right, Length::points(0.f));
    m_sceneImage->layout().setPosition(Edge::Bottom, Length::points(0.f));
    addSubview(m_sceneImage);

    m_placeholder = std::make_shared<Label>("3D viewport");
    m_placeholder->setFont(Typo::Caption);
    m_placeholder->setTextRole(TextRole::Dim);
    m_placeholder->setUserInteractionEnabled(false);
    addSubview(m_placeholder);

    // Rubber-band selection marquee: absolute, semi-transparent gray, above the scene, does not
    // intercept clicks.
    m_marquee = std::make_shared<Panel>();
    m_marquee->setUserInteractionEnabled(false);
    m_marquee->layout().setPositionType(PositionType::Absolute);
    m_marquee->setBackgroundColor(Color(0x64646466));
    hideMarquee();
    addSubview(m_marquee);
}

void ViewportView::setSceneTexture(TextureHandle texture, bool flipVertical) {
    m_sceneImage->setTexture(texture);
    m_sceneImage->setFlipVertical(flipVertical);
    const bool hasTexture = texture.isValid();
    m_placeholder->setHidden(hasTexture);
    m_placeholder->layout().setDisplay(hasTexture ? Display::None : Display::Flex);
}

bool ViewportView::pointerDown(PointerEvent &event) {
    if (Context *context = getContext()) { context->setFocusedView(this); } // focus → gizmo keys/camera
    if (event.button != PointerButton::Left) {
        return false; // camera rotation/movement (RMB/trackpad/WASD) — in CameraController, not here
    }
    if (m_gizmoActive) {
        return false; // LMB over a gizmo handle → goes to it (through Panda::Input), not rect-select
    }
    if (m_toolCapture) {
        return false; // a viewport tool is driving → LMB goes to it (through Panda::Input), not rect-select
    }
    if (m_gameInput && !event.isAltDown) {
        return false; // PLAY: the click belongs to the game (digging etc.); Alt+LMB — editor pick
    }
    m_pressed = true;
    m_pressPosition = event.position;
    m_dragAdditive = event.isShiftDown || event.isControlDown || event.isSuperDown;
    // Rect-select start (degenerate rect). A click without movement becomes 1×1 = single selection.
    const Point local = toLocal(m_pressPosition);
    m_output->viewportViewDidRectSelect(local, local, m_dragAdditive, false);
    return true; // capture: move/up are caught for the rectangle
}

bool ViewportView::pointerMove(PointerEvent &event) {
    if (!m_pressed) { return false; }
    if (std::abs(event.position.x - m_pressPosition.x) > CLICK_SLOP ||
        std::abs(event.position.y - m_pressPosition.y) > CLICK_SLOP) {
        positionMarquee(m_pressPosition, event.position); // dragging — show the rubber-band marquee
    }
    m_output->viewportViewDidRectSelect(toLocal(m_pressPosition), toLocal(event.position), m_dragAdditive, false);
    return true;
}

bool ViewportView::pointerUp(PointerEvent &event) {
    if (event.button == PointerButton::Left && m_pressed) {
        hideMarquee();
        m_output->viewportViewDidRectSelect(toLocal(m_pressPosition), toLocal(event.position), m_dragAdditive, true);
        m_pressed = false;
        return true;
    }
    return false; // not captured here (the LMB went to the gizmo) → let the up through to Panda::Input
}

Point ViewportView::toLocal(Point screenPoint) const {
    const Rect wf = getWorldFrame();
    return Point(screenPoint.x - wf.origin.x, screenPoint.y - wf.origin.y);
}

void ViewportView::positionMarquee(Point startScreen, Point currentScreen) {
    const Rect wf = getWorldFrame();
    const float x = std::min(startScreen.x, currentScreen.x) - wf.origin.x;
    const float y = std::min(startScreen.y, currentScreen.y) - wf.origin.y;
    const float w = std::abs(currentScreen.x - startScreen.x);
    const float h = std::abs(currentScreen.y - startScreen.y);
    m_marquee->layout().setPosition(Edge::Left, Length::points(x));
    m_marquee->layout().setPosition(Edge::Top, Length::points(y));
    m_marquee->layout().setSize({w, h});
    m_marquee->setHidden(false);
    m_marquee->layout().setDisplay(Display::Flex);
}

void ViewportView::hideMarquee() {
    m_marquee->setHidden(true);
    m_marquee->layout().setDisplay(Display::None);
}

bool ViewportView::keyDown(KeyEvent &event) {
    if (m_gameInput) { return false; } // PLAY: the keys belong to the game (hotbar 1-0 etc.)
    switch (event.key) {
        case Key::Key1: m_output->viewportViewDidRequestGizmoMode(0); return true;
        case Key::Key2: m_output->viewportViewDidRequestGizmoMode(1); return true;
        case Key::Key3: m_output->viewportViewDidRequestGizmoMode(2); return true;
        default: return false; // WASD/trackpad → camera (CameraController), the rest → shortcuts
    }
}

} // namespace Editor
