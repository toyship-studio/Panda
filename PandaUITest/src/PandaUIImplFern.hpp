#pragma once

#include <PandaUI/Runtime/Context.hpp>

namespace Fern {
class Window;
struct Event;
} // namespace Fern

class PandaUIImplFern final {
public:
    ~PandaUIImplFern();

    void init(Fern::Window *window, PandaUI::Context &context);
    void shutdown();
    bool handleEvent(Fern::Event *event);
    void syncViewport();

    bool isInitialized() const {
        return m_initialized;
    }

    PandaUI::Context &context();

private:
    void updateModifierState(PandaUI::Key key, bool isDown);
    PandaUI::KeyEvent makeKeyEvent(PandaUI::Key key) const;
    void applyModifiers(PandaUI::PointerEvent &event) const;
    void applyCursor();

    Fern::Window *m_window = nullptr;
    PandaUI::Context *m_context = nullptr;
    bool m_initialized = false;
    bool m_leftShiftDown = false;
    bool m_rightShiftDown = false;
    bool m_leftControlDown = false;
    bool m_rightControlDown = false;
    bool m_leftAltDown = false;
    bool m_rightAltDown = false;
    bool m_leftSuperDown = false;
    bool m_rightSuperDown = false;
};
