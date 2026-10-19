#include <PandaRuntime/PandaRuntime.hpp>

#include "RuntimeLayer.hpp"

#include <Fern/Fern.hpp>
#include <Miren/Miren.hpp>
#include <Panda/Application/Application.hpp>
#include <Panda/Runtime/RuntimeManifest.hpp>

#include "ProjectLoader/RuntimeProjectLoader.hpp"

#include <Foundation/PlatformDetection.hpp>

#include <filesystem>

namespace {

#if defined(PLATFORM_IOS)
struct MobileRuntimeState {
    Panda::Application *application = nullptr;
    Fern::Window *window = nullptr;
};

MobileRuntimeState s_mobileRuntimeState;

void tickMobileRuntime(double deltaTime, void *userData) {
    auto *state = static_cast<MobileRuntimeState *>(userData);
    if (!state || !state->application) { return; }
    if (!state->application->tick(deltaTime)) { Fern::clearFrameCallback(); }
}
#endif

std::filesystem::path getRuntimeDataPath() {
    const std::filesystem::path &resourcesPath = Fern::getStaticResourcesPath();
    if (resourcesPath.empty()) { LOG_ERROR("RUNTIME: Static resources path is not set"); }
    return resourcesPath / ".Panda";
}

} // namespace

namespace Panda::Runtime {

int run() {
    Fern::initialize();
    const std::filesystem::path runtimeDataPath = getRuntimeDataPath();

    RuntimeManifest manifest;
    if (!RuntimeProjectLoader::loadManifest(runtimeDataPath, manifest)) {
        LOG_ERROR("RUNTIME: Failed to load manifest from %s", runtimeDataPath.string().c_str());
        Fern::terminate();
        return 1;
    }

    Fern::Rect rect(0, 0, 1280, 720);
    Fern::Window *window = Fern::createWindow("Panda Game", rect, Fern::WindowState::WindowStateNormal);

    Miren::initialize(window->getDrawingContext());

    auto *application = new Panda::Application();
    application->setMainWindow(window);
    application->pushLayer(new Panda::RuntimeLayer(runtimeDataPath));

#if defined(PLATFORM_IOS)
    s_mobileRuntimeState.application = application;
    s_mobileRuntimeState.window = window;
    Fern::setFrameCallback(tickMobileRuntime, &s_mobileRuntimeState);
    return 0;
#else
    application->loop();

    delete application;
    Miren::terminate();
    Fern::disposeWindow(window);
    Fern::terminate();
    return 0;
#endif
}

} // namespace Panda::Runtime
