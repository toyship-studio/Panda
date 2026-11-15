#pragma once

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
// The main window of the game is the window of the application; while the game plays inside the
// editor it is a frame of that window instead (setMainViewportFrame: the Game tab), and the UI
// draws into GAME_UI_VIEW, which the host of the frame points at its target.
class GameUI final {
public:
    explicit GameUI(Fern::Window *mainWindow);

    void initialize();
    void shutdown();
    // The UI of the bundle lays out, animates and submits its draw lists.
    void update(float deltaTime);
    // The window was resized: its view follows.
    void syncWindow(Fern::Window *window);
    // An event of a window of the game UI into the bundle; true — a view consumed it.
    bool handleEvent(Fern::Event *event);
    void setMainViewportFrame(Rect frame, Size dpiScale);
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
