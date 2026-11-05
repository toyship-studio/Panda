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
    void applyCursor();

    Fern::Window *m_window = nullptr;
    PandaUI::Context *m_context = nullptr;
    bool m_initialized = false;
};
