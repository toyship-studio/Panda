#include "Panda/GameLogic/GameContext.hpp"
#include "Panda/Application/Application.hpp"
#include "Panda/Extensions/Extensions.hpp"
#include "Panda/Packages/PackageLoader.hpp"

#ifdef PND_EDITOR
#    include "Panda/GameLogic/SelectionContext.hpp"
#endif

#include <utility>

namespace Panda {

World *GameContext::s_currentWorld = nullptr;
AssetHandler *GameContext::s_assetHandler = nullptr;
ScriptEngine *GameContext::s_scriptEngine = nullptr;
GameContext::WorldLoadRequestHandler GameContext::s_worldLoadRequestHandler = nullptr;
GameContext::QuitRequestHandler GameContext::s_quitRequestHandler = nullptr;
path_t GameContext::s_persistentDataPath;
std::unordered_set<AssetId> GameContext::s_runtimeMutatedAssets;

Extensions &GameContext::getExtensions() {
    static Extensions extensions;
    return extensions;
}

PackageLoader &GameContext::getPackageLoader() {
    static PackageLoader loader;
    return loader;
}

World *GameContext::getCurrentWorld() {
    return s_currentWorld;
}

void GameContext::setCurrentWorld(World *world) {
    s_currentWorld = world;
#ifdef PND_EDITOR
    if (!world) {
        SelectionContext::unselectAll();
        return;
    }
    std::unordered_set<UUID> selectedIds = SelectionContext::getSelectedEntities();
    for (UUID id : selectedIds) {
        if (!world->isValid(id)) { SelectionContext::removeSelectedEntity(id, false); }
    }
    SelectionContext::updateValues();
#endif
}

AssetHandler *GameContext::getAssetHandler() {
    return s_assetHandler;
}

void GameContext::setAssetHandler(AssetHandler *handler) {
    s_assetHandler = handler;
}

ScriptEngine *GameContext::getScriptEngine() {
    return s_scriptEngine;
}

void GameContext::setScriptEngine(ScriptEngine *engine) {
    s_scriptEngine = engine;
}

void GameContext::setWorldLoadRequestHandler(WorldLoadRequestHandler handler) {
    s_worldLoadRequestHandler = std::move(handler);
}

void GameContext::requestWorldLoad(AssetId worldAssetId) {
    if (!worldAssetId) {
        LOG_ERROR("WORLD LOAD REQUEST HAS EMPTY WORLD HANDLE");
        return;
    }
    if (!s_worldLoadRequestHandler) {
        LOG_ERROR("WORLD LOAD REQUEST HANDLER IS NOT SET");
        return;
    }
    s_worldLoadRequestHandler(worldAssetId);
}

void GameContext::setQuitRequestHandler(QuitRequestHandler handler) {
    s_quitRequestHandler = std::move(handler);
}

void GameContext::setPersistentDataPath(const path_t &path) {
    s_persistentDataPath = path;
}

const path_t &GameContext::getPersistentDataPath() {
    return s_persistentDataPath;
}

void GameContext::markRuntimeMutatedAsset(AssetId id) {
    if (id != AssetId(0)) { s_runtimeMutatedAssets.insert(id); }
}

std::unordered_set<AssetId> GameContext::takeRuntimeMutatedAssets() {
    return std::exchange(s_runtimeMutatedAssets, {});
}

void GameContext::requestQuit() {
    if (s_quitRequestHandler) {
        s_quitRequestHandler();
        return;
    }
    Application::get()->close();
}

} // namespace Panda
