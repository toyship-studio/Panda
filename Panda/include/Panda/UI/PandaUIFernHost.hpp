#pragma once

#include <Fern/Window/Cursor.hpp>
#include <PandaUI/Core/Cursor.hpp>
#include <PandaUI/Core/Event.hpp>

namespace Fern {
struct Event;
class Window;
} // namespace Fern

namespace PandaUI {
class Context;
}

namespace Panda {

// PandaUI on the Fern platform: the one translation of what a window reports — keys, text, the
// pointer, the wheel and the trackpad, touches — into PandaUI events, the cursor, the clipboard.
// PandaUI and Fern do not know each other; this is where they meet, for every host of a PandaUI
// context (GameUI, PandaUIFernHost).
namespace PandaUIFern {

    // The type and the payload of the PandaUI event a Fern event of `window` means; `origin` is
    // where the UI begins inside the window (a game UI drawn in a frame of the editor; zero for a
    // UI that fills the window). The window fields of the event (handle, size, scale, safe area)
    // are the host's to fill. false — the event means nothing to a UI.
    bool translate(Fern::Event *event, Fern::Window &window, PandaUI::Point origin, PandaUI::Event &out);
    Fern::Cursor toFernCursor(PandaUI::Cursor cursor);
    // The copy and paste of PandaUI over the clipboard of the system. Once per process.
    void bindSystemClipboard();

} // namespace PandaUIFern

// A PandaUI context that lives in this process, in a window of its own: the size, the scale and
// the safe area of the window, its input and the cursor. The UI of an application built on the
// engine (the editor: the main window and the Settings window).
class PandaUIFernHost final {
public:
    ~PandaUIFernHost();

    void init(Fern::Window *window, PandaUI::Context &context);
    void shutdown();
    bool isInitialized() const {
        return m_context != nullptr;
    }
    // An event of the window into the context; true — a view consumed it. Events of other
    // windows are not this host's.
    bool handleEvent(Fern::Event *event);
    // The size, the scale and the safe area of the window into the context (after a resize;
    // once a frame for a window whose resize events are not routed here).
    void syncViewport();

private:
    Fern::Window *m_window = nullptr;
    PandaUI::Context *m_context = nullptr;
};

} // namespace Panda
