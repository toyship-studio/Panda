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

#include <cstdlib>
#include <filesystem>
#include <string_view>

int main(int argc, const char **argv) {
    // The last lines of the log stay in memory from the first one: the MCP server of a project
    // answers with them (App/EditorMcp), also about what happened before it started.
    Foundation::LogHistory::enable();
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
    // The command line. `--new-window`: a second window is a second instance, it starts on the
    // start screen — the project of the last session belongs to the first one. `--project <path>`:
    // the instance opens this project (a restart, an agent or a script that starts the editor).
    // `--mcp <port>`: the MCP server listens from the start, with or without a project.
    Panda::EditorLaunch launch;
    for (int i = 1; i < argc; i++) {
        const std::string_view argument(argv[i]);
        if (argument == Panda::SystemTools::NEW_WINDOW_ARGUMENT) {
            launch.openLastProject = false;
        } else if (argument == Panda::SystemTools::PROJECT_ARGUMENT && i + 1 < argc) {
            launch.project = std::filesystem::absolute(Panda::path_t(argv[++i])).lexically_normal();
        } else if (argument == Panda::SystemTools::MCP_ARGUMENT && i + 1 < argc) {
            launch.mcpPort = std::atoi(argv[++i]);
        }
    }
    application->pushLayer(new Panda::EditorLayer(window, std::move(launch)));
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
    if (!Panda::EditorUpdater::finishOnExit(relaunch) && relaunch) {
        Panda::SystemTools::startNewInstance(Panda::SystemTools::relaunchArguments());
    }
    return 0;
}
