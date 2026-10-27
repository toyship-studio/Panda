#pragma once

#include <PandaUI/PandaUI.hpp>

#include <functional>
#include <memory>
#include <string>

namespace PandaUI::Gallery {

// An interactive area that reports incoming events through a callback.
class EventProbe final : public PandaUI::Control {
public:
    explicit EventProbe(std::function<void(const std::string &)> onEvent);

private:
    void addSubviews();
    void setupLayout();
    void setupStyle();

    bool pointerDown(PandaUI::PointerEvent &event) override;
    bool pointerUp(PandaUI::PointerEvent &event) override;
    bool scroll(PandaUI::ScrollEvent &event) override;
    bool keyDown(PandaUI::KeyEvent &event) override;
    void focusChanged(bool focused) override;
    void controlStateChanged(PandaUI::ControlState previous, PandaUI::ControlState current) override;
    void themeChanged() override;

    std::function<void(const std::string &)> m_onEvent;
    std::shared_ptr<PandaUI::Label> m_hint;
    bool m_focused = false;
};

} // namespace PandaUI::Gallery
