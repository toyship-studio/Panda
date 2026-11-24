#pragma once

#include <Panda/Assets/Base/Asset.hpp> // AssetId
#include <Panda/GameLogic/World.hpp>
#include <Panda/UI/GameUI.hpp>

#include <cstdint>
#include <functional>

namespace Fern {
struct Event;
class Window;
} // namespace Fern

namespace Panda {

class EditorViewports;

// The game inside the editor (Play). It runs in a copy of the world being edited — its physics,
// its scripts — draws its UI over the Game surface and owns the input of the window while the
// Game has it; Stop drops what the copy became. The session is the game alone: what the
// editor shows of it — the selection, the panels, the tabs — is the application's
// (EditorLayer::play, stop).
class PlaySession final {
public:
    PlaySession(Fern::Window *window, EditorViewports &viewports);

    void initialize(); // a script that quits the application stops the game instead
    void shutdown();

    bool isPlaying() const {
        return m_playing;
    }
    // The playing copy: the world the editor shows while the game plays.
    World &world() {
        return m_world;
    }

    // ---- Play in two steps, with the editor's own in between: the world tab shows the Game and
    // ---- the viewports take its size before the game starts — its UI and its scripts read them.
    void begin(World &source); // the copy is made; nothing runs yet and the game has no input
    void run();                      // the UI of the game is there, then its scripts start
    // Stop: the copy runs no more, the UI and the input of the game are gone.
    void end();

    // ---- Once a frame.
    // A script asked to quit. In the editor that is Stop, and it waits for the next frame: the
    // request arrives in the middle of the world update, where tearing the playing world down
    // would crash the script frame in progress. True once per request.
    bool takeQuitRequest();
    // The game owns the input of the window while it plays, the Game renders, its tab has the
    // keyboard focus (a click on the game gives it, a click anywhere else in the editor takes it
    // away), no dialog of the editor is open and the window is focused. Panda::Input is the
    // game's input (the events are routed by ownsEvent); this is its focus, and a transition is
    // the application focus of the scripts (onApplicationFocus). The loss releases what the game
    // held (the releases will go to the editor) and the cursor lock: an FPS is left by clicking
    // elsewhere, and locks again in onApplicationFocus(true).
    void updateInput(bool gameTabFocused, bool modalOpen);
    // An agent plays the game (the game_input tool of the MCP server): while it does, the game
    // has the input whatever has the focus — the editor of a test runs in the background.
    void setDriven(bool driven);
    // The copy runs. Not while it waits for the world a script asked for: it keeps its last
    // frame then.
    bool isRunning() {
        return m_world.isRunning();
    }
    void simulate(double deltaTime); // a step of the game
    // After the frame of the game is drawn: its UI over the Game surface, then the world a
    // script asked for is loaded — between frames.
    void updateUi(double deltaTime);

    // ---- The events of the window.
    // The focus and the minimizing of the editor window: the focus of the game follows the first
    // (updateInput), a minimized editor pauses the scripts.
    void windowEvent(const Fern::Event &event);
    void windowResized(); // the UI of the game follows the size
    // Whether an input event is the game's while it plays and the Game is on screen: the pointer
    // over the Game surface — `overGame(x, y)`: the surface is what lies under that point of the
    // window, not a menu, a popup or a dialog of the editor over it — and the keys while the Game
    // has the focus. The release of a button goes where its press went: a drag that started on
    // the game may leave the surface, one that started on the chrome (a splitter) may end over it.
    bool ownsEvent(
        const Fern::Event &event, bool gameTabFocused, bool modalOpen, const std::function<bool(double x, double y)> &overGame
    );
    // An event the game owns goes to its UI first; true — a view of it took the event. What is
    // left Application feeds to Panda::Input.
    bool handleEvent(Fern::Event *event);

private:
    void syncUiFrame(); // the UI of the game follows the Game surface
    // The game asked for another world (Bamboo): the file is read into the playing copy between
    // frames.
    void loadRequestedWorld();
    bool loadWorld(AssetId worldAsset);

    Fern::Window *m_window;
    EditorViewports &m_viewports;
    World m_world;   // Play runs in a copy of the edited world
    GameUI m_gameUI; // the UI of the game while it plays: drawn into the Game surface
    bool m_playing = false;
    bool m_quitRequested = false;
    bool m_windowFocused = true; // the editor window has the keyboard focus
    bool m_inputActive = false;  // the game owns the input
    bool m_driven = false;       // setDriven
    uint32_t m_mouseButtons = 0; // the buttons the game pressed (bit = Fern::MouseButton): their releases are its
    AssetId m_requestedWorld = AssetId(0); // the world a script asked for; 0 — none
};

} // namespace Panda
