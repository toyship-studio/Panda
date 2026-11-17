#include "App/PlaySession.hpp"

#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Viewport/EditorViewports.hpp"

#include <Fern/Events/MouseEvents.hpp>
#include <Fern/Events/TouchEvents.hpp>
#include <Fern/Events/WindowEvents.hpp>
#include <Fern/Fern.hpp>
#include <Foundation/Logger.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/GameLogic/Input.hpp>
#include <Panda/GameLogic/Systems/ScriptSystem.hpp>
#include <Panda/Serialization/WorldMapper.hpp>
#include <Rain/Coders/JsonDecoder.hpp>

#include <fstream>

namespace Panda {

PlaySession::PlaySession(Fern::Window *window, EditorViewports &viewports)
    : m_window(window)
    , m_viewports(viewports)
    , m_gameUI(window) {}

void PlaySession::initialize() {
    GameContext::setQuitRequestHandler([this] { m_quitRequested = true; });
}

void PlaySession::shutdown() {
    m_gameUI.shutdown();
    GameContext::setQuitRequestHandler(nullptr);
    GameContext::setWorldLoadRequestHandler(nullptr);
}

void PlaySession::begin(World &source) {
    m_playing = true;
    m_world = source;
    GameContext::setWorldLoadRequestHandler([this](AssetId worldAsset) { m_requestedWorld = worldAsset; });
    m_requestedWorld = AssetId(0);
    m_inputActive = false; // updateInput hands the input to the game when its tab renders and has the focus
    m_mouseButtons = 0;
    Input::setFocused(false);
}

void PlaySession::run() {
    syncUiFrame();
    m_gameUI.initialize();
    m_world.startRunning();
    loadRequestedWorld();
}

void PlaySession::end() {
    if (!m_playing) { return; }
    m_world.finishRunning();
    Fern::setCursorLocked(false);
    m_gameUI.shutdown();
    m_gameUI.clearMainViewportFrame();
    m_playing = false;
    m_inputActive = false;
    m_driven = false;
    Input::setCursorVirtual(false);
    m_mouseButtons = 0;
    Input::setFocused(false);
    Input::releaseAll(); // what the game still held: its releases go to the editor now
    m_requestedWorld = AssetId(0);
    GameContext::setWorldLoadRequestHandler(nullptr);
}

void PlaySession::setDriven(bool driven) {
    if (m_driven == driven) { return; }
    m_driven = driven;
    // The cursor of a driven game is its own: the real one is let go and stays with the user.
    Input::setCursorVirtual(driven);
    if (driven) { Fern::setCursorLocked(false); }
}

bool PlaySession::takeQuitRequest() {
    const bool requested = m_quitRequested;
    m_quitRequested = false;
    return requested;
}

void PlaySession::updateInput(bool gameTabFocused, bool modalOpen) {
    const bool active = m_playing && m_viewports.isGameLive() && !modalOpen && (m_driven || (m_windowFocused && gameTabFocused));
    if (active == m_inputActive) { return; }
    m_inputActive = active;
    Input::setFocused(active);
    if (!active) {
        Input::releaseAll();
        Fern::setCursorLocked(false);
    }
    if (m_world.isRunning()) { ScriptSystem::applicationFocus(m_world, active); }
}

void PlaySession::simulate(double deltaTime) {
    m_world.update(deltaTime);
}

void PlaySession::updateUi(double deltaTime) {
    syncUiFrame();
    m_gameUI.update(static_cast<float>(deltaTime)); // the game UI draws into the Game surface
    loadRequestedWorld();
}

void PlaySession::windowEvent(const Fern::Event &event) {
    if (event.getWindow() != nullptr && event.getWindow() != m_window) { return; }
    switch (event.type) {
        case Fern::EventType::WindowFocus: m_windowFocused = true; break;
        case Fern::EventType::WindowLostFocus: m_windowFocused = false; break;
        case Fern::EventType::WindowMinimized:
            if (m_playing) { ScriptSystem::applicationPause(m_world, true); }
            break;
        case Fern::EventType::WindowRestored:
            if (m_playing) { ScriptSystem::applicationPause(m_world, false); }
            break;
        default: break;
    }
}

void PlaySession::windowResized() {
    if (!m_playing) { return; }
    syncUiFrame();
    m_gameUI.syncWindow(m_window);
}

bool PlaySession::ownsEvent(const Fern::Event &event, bool gameTabFocused, bool modalOpen) {
    const auto inGameFrame = [this](double x, double y) {
        const Rect &frame = m_viewports.gameFrame();
        return x >= frame.origin.x && y >= frame.origin.y && x < frame.origin.x + frame.size.width &&
               y < frame.origin.y + frame.size.height;
    };
    const auto cursorInGameFrame = [this, &inGameFrame]() {
        const Fern::Vec2 cursor = m_window->getCursorPosition();
        return inGameFrame(cursor.x, cursor.y);
    };
    // A cursor the game locked is the game's wherever it is said to be: the lock put it into the
    // middle of the window, which may lie outside the Game surface. (The lock ends with the focus
    // of the game: updateInput.)
    const bool locked = Fern::isCursorLocked();
    switch (event.type) {
        case Fern::EventType::MouseMoved: {
            const auto &moved = static_cast<const Fern::MouseMovedEvent &>(event);
            return locked || m_mouseButtons != 0 || (!modalOpen && inGameFrame(moved.x, moved.y));
        }
        case Fern::EventType::MouseButtonPressed: {
            const auto &pressed = static_cast<const Fern::MouseKeyEvent &>(event);
            const uint32_t bit = 1u << static_cast<uint32_t>(pressed.button);
            const bool game = locked || (!modalOpen && cursorInGameFrame());
            m_mouseButtons = game ? (m_mouseButtons | bit) : (m_mouseButtons & ~bit);
            return game;
        }
        case Fern::EventType::MouseButtonReleased: {
            const auto &released = static_cast<const Fern::MouseKeyEvent &>(event);
            const uint32_t bit = 1u << static_cast<uint32_t>(released.button);
            if ((m_mouseButtons & bit) == 0) { return false; }
            m_mouseButtons &= ~bit;
            return true;
        }
        case Fern::EventType::MouseScrolled:
            return locked || (!modalOpen && cursorInGameFrame());
        case Fern::EventType::TouchBegan: {
            const auto &touch = static_cast<const Fern::TouchBeganEvent &>(event);
            return !modalOpen && inGameFrame(touch.x, touch.y);
        }
        case Fern::EventType::TouchMoved: {
            const auto &touch = static_cast<const Fern::TouchMovedEvent &>(event);
            return !modalOpen && inGameFrame(touch.x, touch.y);
        }
        case Fern::EventType::TouchEnded:
        case Fern::EventType::KeyPressed:
        case Fern::EventType::KeyReleased:
        case Fern::EventType::InputCharacter:
            return !modalOpen && gameTabFocused;
        default:
            return false;
    }
}

bool PlaySession::handleEvent(Fern::Event *event) {
    syncUiFrame();
    return m_gameUI.handleEvent(event);
}

void PlaySession::syncUiFrame() {
    const Fern::Size dpi = m_window->getDpi();
    // The game UI lives in the Game tab: drawn over the output of the Game while it renders.
    m_gameUI.setMainViewportFrame(m_viewports.gameFrame(), Size(dpi.width, dpi.height), m_viewports.gameTarget());
}

void PlaySession::loadRequestedWorld() {
    if (!m_requestedWorld || !m_playing) { return; }
    const AssetId worldAsset = m_requestedWorld;
    m_requestedWorld = AssetId(0);
    if (!loadWorld(worldAsset)) { LOG_ERROR_EDITOR("Failed to load requested world %u", (uint32_t)worldAsset); }
}

bool PlaySession::loadWorld(AssetId worldAsset) {
    AssetHandlerEditor *assetHandler = AssetHandlerEditor::current();
    const path_t worldPath = assetHandler != nullptr ? assetHandler->worldFilePath(worldAsset) : path_t();
    if (worldPath.empty()) { return false; }
    std::ifstream file(worldPath);
    if (!file.is_open()) {
        LOG_ERROR_EDITOR("World file not found: %s", worldPath.string().c_str());
        return false;
    }

    const bool wasRunning = m_world.isRunning();
    if (wasRunning) { m_world.finishRunning(); }
    Rain::JsonDecoder decoder;
    const bool loaded = WorldMapper::load(m_world, decoder, file);
    file.close();
    if (!loaded) {
        LOG_ERROR_EDITOR("Failed to decode world: %s", worldPath.string().c_str());
        if (wasRunning) { m_world.startRunning(); }
        return false;
    }
    m_world.updateScriptsAndFields();
    m_viewports.scene().setWorld(&m_world);
    if (wasRunning) { m_world.startRunning(); }
    return true;
}

} // namespace Panda
