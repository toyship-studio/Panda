#pragma once

#include "PandaUI/Core/View.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace PandaUI {

enum class ControlState : uint8_t {
    Normal = 0,
    Hovered = 1 << 0,
    Highlighted = 1 << 1,
    Disabled = 1 << 2,
    Selected = 1 << 3,
    Focused = 1 << 4,
};

constexpr ControlState operator|(ControlState lhs, ControlState rhs) {
    return static_cast<ControlState>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

constexpr ControlState operator&(ControlState lhs, ControlState rhs) {
    return static_cast<ControlState>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
}

constexpr ControlState &operator|=(ControlState &lhs, ControlState rhs) {
    lhs = lhs | rhs;
    return lhs;
}

constexpr bool contains(ControlState state, ControlState flag) {
    return (static_cast<uint8_t>(state & flag) != 0);
}

enum class ControlEvent : uint8_t {
    PrimaryActionTriggered,
    ValueChanged,
};

class Control : public View {
public:
    using ActionCallback = std::function<void(Control &)>;

    Control();

    ControlState controlState() const {
        return m_controlState;
    }
    bool hasControlState(ControlState state) const {
        return contains(m_controlState, state);
    }

    void setSelected(bool selected);
    bool isSelected() const {
        return m_isSelected;
    }

    bool isHighlighted() const {
        return m_isHighlighted;
    }

    void addAction(ControlEvent event, ActionCallback action);
    void removeActions(ControlEvent event);
    void setPrimaryAction(ActionCallback action);

    bool pointerDown(PointerEvent &event) override;
    bool pointerUp(PointerEvent &event) override;
    bool pointerMove(PointerEvent &event) override;
    bool keyDown(KeyEvent &event) override;
    void hoverChanged(bool hovered) override;
    void enabledChanged(bool enabled) override;
    void focusChanged(bool focused) override;
    void interactionCancelled() override;

protected:
    virtual bool acceptsPointer(const PointerEvent &event) const;
    virtual void performPrimaryAction();
    virtual void controlStateChanged(ControlState previousState, ControlState currentState);

    void setHighlighted(bool highlighted);
    void sendActions(ControlEvent event);

private:
    struct ActionEntry {
        ControlEvent event;
        ActionCallback callback;
    };

    bool isTrackingPointer(const PointerEvent &event) const;
    void updateControlState();

    std::vector<ActionEntry> m_actions;
    ControlState m_controlState;
    bool m_isSelected;
    bool m_isHighlighted;
    bool m_isTracking;
    PointerType m_trackingPointerType;
    int m_trackingPointerId;
};

} // namespace PandaUI
