#pragma once

#include <Miren/Base.hpp>
#include <Panda/Base/Base.hpp>
#include <Panda/ScriptEngine/ABI/ScriptApi.hpp>

namespace Fern {
struct Event;
class Window;
} // namespace Fern

namespace Panda {

// The UI of the game. Its PandaUI contexts live in the script bundle (Bamboo::UIRuntime); the
// engine gives them a place to be: the windows, the input of those windows, the clipboard, the
// resources, the textures and the drawing — all through the `ui_*` entries of the script ABI
// (bindHostApi). The translation of the input and the drawing are the shared ones
// (PandaUIFernHost.hpp, PandaUIMirenRenderer.hpp).
//
// The main window of the game is the window of the application, and its UI is drawn over the
// finished frame, into the back buffer. While the game plays inside the editor it is a frame of
// that window instead (setMainViewportFrame: the Game tab), drawn into the target the editor
// names — over the frame of the game the editor put there earlier in the same Miren frame. A
// window is a pass of the frame from its first draw list on: the UI is updated after the frame
// of the game is developed.
class GameUI final {
public:
    explicit GameUI(Fern::Window *mainWindow);

    void initialize();
    void shutdown();
    // The UI of the bundle lays out, animates and submits its draw lists.
    void update(float deltaTime);
    // The window was resized: its drawable follows.
    void syncWindow(Fern::Window *window);
    // An event of a window of the game UI into the bundle; true — a view consumed it.
    bool handleEvent(Fern::Event *event);
    // The main window is the frame of another window (in points of that window, its dpi scale),
    // drawn into `target` — a framebuffer of frame.size × dpiScale pixels. An invalid target: the
    // host does not draw the frame now, the UI is updated and not drawn.
    void setMainViewportFrame(Rect frame, Size dpiScale, Miren::FrameBufferHandle target);
    void clearMainViewportFrame();

    static void bindHostApi(ScriptHostApi &hostApi);
    // Once a frame, after Miren::frame: the windows the UI closed go when no frame in flight
    // draws into them any more.
    static void flushPendingWindowDisposals();

private:
    Fern::Window *m_window;
    bool m_initialized = false;
};

} // namespace Panda
