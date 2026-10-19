#include "PandaUI/Elements/Control.hpp"

#include <algorithm>
#include <utility>

namespace PandaUI {

Control::Control()
    : m_controlState(ControlState::Normal)
    , m_isSelected(false)
    , m_isHighlighted(false)
    , m_isTracking(false)
    , m_trackingPointerType(PointerType::Mouse)
    , m_trackingPointerId(0) {
    setFocusable(true);
}

void Control::setSelected(bool selected) {
    if (m_isSelected == selected) { return; }
    m_isSelected = selected;
    updateControlState();
}

void Control::addAction(ControlEvent event, ActionCallback action) {
    m_actions.push_back({event, std::move(action)});
}

void Control::removeActions(ControlEvent event) {
    std::erase_if(m_actions, [event](const ActionEntry &entry) { return entry.event == event; });
}

void Control::setPrimaryAction(ActionCallback action) {
    removeActions(ControlEvent::PrimaryActionTriggered);
    if (action) { addAction(ControlEvent::PrimaryActionTriggered, std::move(action)); }
}

bool Control::pointerDown(PointerEvent &event) {
    if (m_isTracking) { return false; }
    if (!acceptsPointer(event)) { return false; }

    m_isTracking = true;
    m_trackingPointerType = event.type;
    m_trackingPointerId = event.pointerId;
    setHighlighted(true);
    return true;
}

bool Control::pointerUp(PointerEvent &event) {
    if (!m_isTracking || !isTrackingPointer(event)) { return false; }

    const bool shouldPerformAction = acceptsPointer(event) && pointInside(event.position);
    m_isTracking = false;
    setHighlighted(false);
    if (shouldPerformAction) { performPrimaryAction(); }
    return true;
}

bool Control::pointerMove(PointerEvent &event) {
    if (!m_isTracking || !isTrackingPointer(event)) { return false; }
    setHighlighted(pointInside(event.position));
    return true;
}

bool Control::keyDown(KeyEvent &event) {
    if (!isEnabled()) { return false; }
    if (event.key != Key::Space && event.key != Key::Enter && event.key != Key::KeypadEnter) { return false; }
    // Activate only on the unmodified key; let modified combos (e.g. a Shift+Space shortcut) bubble up.
    if (event.isShiftDown || event.isControlDown || event.isAltDown || event.isSuperDown) { return false; }

    performPrimaryAction();
    return true;
}

void Control::hoverChanged(bool) {
    updateControlState();
}

void Control::enabledChanged(bool enabled) {
    if (!enabled) {
        m_isTracking = false;
        m_isHighlighted = false;
    }
    updateControlState();
}

void Control::focusChanged(bool) {
    updateControlState();
}

void Control::interactionCancelled() {
    m_isTracking = false;
    setHighlighted(false);
}

bool Control::acceptsPointer(const PointerEvent &event) const {
    if (!isEnabled()) { return false; }
    if (event.type == PointerType::Touch) { return true; }
    return event.button == PointerButton::Left;
}

void Control::performPrimaryAction() {
    sendActions(ControlEvent::PrimaryActionTriggered);
}

void Control::controlStateChanged(ControlState, ControlState) {}

void Control::setHighlighted(bool highlighted) {
    if (m_isHighlighted == highlighted) { return; }
    m_isHighlighted = highlighted;
    updateControlState();
}

void Control::sendActions(ControlEvent event) {
    std::vector<ActionCallback> callbacks;
    callbacks.reserve(m_actions.size());
    for (const ActionEntry &entry : m_actions) {
        if (entry.event == event && entry.callback) { callbacks.push_back(entry.callback); }
    }

    for (const ActionCallback &callback : callbacks) {
        callback(*this);
    }
}

bool Control::isTrackingPointer(const PointerEvent &event) const {
    if (event.type != m_trackingPointerType) { return false; }
    if (event.type == PointerType::Mouse) { return true; }
    return event.pointerId == m_trackingPointerId;
}

void Control::updateControlState() {
    ControlState nextState = ControlState::Normal;
    if (!isEnabled()) {
        nextState |= ControlState::Disabled;
    } else {
        if (isHovered()) { nextState |= ControlState::Hovered; }
        if (m_isHighlighted) { nextState |= ControlState::Highlighted; }
        if (m_isSelected) { nextState |= ControlState::Selected; }
        if (isFocused()) { nextState |= ControlState::Focused; }
    }

    if (m_controlState == nextState) { return; }
    const ControlState previousState = m_controlState;
    m_controlState = nextState;
    controlStateChanged(previousState, m_controlState);
    setNeedsDisplay();
}

} // namespace PandaUI
