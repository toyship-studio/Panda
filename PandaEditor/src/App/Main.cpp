//
//  Application.cpp
//  Main
//

#include "App/EditorLayer.hpp"
#include "App/EditorUpdater.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Project/System/SystemTools.hpp"

#include <Foundation/PlatformDetection.hpp>
#include <Fern/Fern.hpp>
#include <Panda.hpp>

#include <string_view>

int main(int argc, const char **argv) {
    Fern::initialize();
    // The desktop identity on Linux: the class of the .desktop entry the install script writes.
    Fern::setDesktopIdentity("PandaEditor", "dev.pandaengine.PandaEditor");
    Fern::Rect rect = Fern::Rect(0, 0, 1400, 800);
    Fern::Window *window = Fern::createWindow("Panda Editor", rect, Fern::WindowState::WindowStateNormal);
    Miren::initialize(window->getDrawingContext());
    Panda::AssetHandler *assetHandler = new Panda::AssetHandlerEditor;
    Panda::GameContext::setAssetHandler(assetHandler);
    // Modules are loaded by ProjectLoader when a project is opened — the project decides the set
    // (context/plugins.md, decision 8); user plugins are stage 2.
    auto application = new Panda::Application();
    application->setMainWindow(window);
    // A second window is a second instance: it starts on the start screen, the project of the
    // last session belongs to the first one.
    bool newWindow = false;
    for (int i = 1; i < argc; i++) {
        if (std::string_view(argv[i]) == Panda::SystemTools::NEW_WINDOW_ARGUMENT) { newWindow = true; }
    }
    application->pushLayer(new Panda::EditorLayer(window, !newWindow));
    application->loop();
    delete application;
    assetHandler->cleanup(); // dependency cascade inside — to a fixpoint
    delete assetHandler;
    Miren::terminate();
    Fern::disposeWindow(window);
    // A restart asked from the UI (a changed module set, an installed update): the new instance
    // starts only now, after the settings of this one are written and its window is gone. An update
    // staged by the updater is swapped in first (Linux here, Windows by the helper); it starts the
    // new editor itself.
    const bool relaunch = Panda::SystemTools::relaunchOnExit();
    if (!Panda::EditorUpdater::finishOnExit(relaunch) && relaunch) { Panda::SystemTools::startNewInstance(); }
    return 0;
}
