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

int main(int argc, const char **argv) {
    Fern::initialize();
    Fern::Rect rect = Fern::Rect(0, 0, 1400, 800);
    Fern::Window *window = Fern::createWindow("Panda Editor", rect, Fern::WindowState::WindowStateNormal);
    Miren::initialize(window->getDrawingContext());
    Panda::AssetHandler *assetHandler = new Panda::AssetHandlerEditor;
    Panda::GameContext::setAssetHandler(assetHandler);
    // Modules are loaded by ProjectLoader when a project is opened — the project decides the set
    // (context/plugins.md, decision 8); user plugins are stage 2.
    auto application = new Panda::Application();
    application->setMainWindow(window);
    application->pushLayer(new Panda::EditorLayer(window));
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
    if (!Panda::EditorUpdater::finishOnExit(relaunch) && relaunch) { Panda::SystemTools::relaunchApplication(); }
    return 0;
}
