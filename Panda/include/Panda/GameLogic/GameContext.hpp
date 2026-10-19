#pragma once

#include "Panda/Assets/Base/AssetHandler.hpp"
#include "Panda/ScriptEngine/ScriptEngine.hpp"

#include <functional>
#include <unordered_set>

namespace Panda {

class World;
class Extensions;
class PackageLoader;

class GameContext {
public:
    // Engine registries (context/plugins.md): core contributions exist from birth, modules add
    // theirs on load (PackageLoader). Contributions after startup — see
    // World::prepareComponentTypes and ResourceProviderRegistry::setLive.
    static Extensions &getExtensions();
    // Loader of the engine parts of modules: static (in-tree, export) and dynamic.
    static PackageLoader &getPackageLoader();
    using WorldLoadRequestHandler = std::function<void(AssetId)>;

    static World *getCurrentWorld();
    static void setCurrentWorld(World *world);
    static AssetHandler *getAssetHandler();
    static void setAssetHandler(AssetHandler *handler);
    static ScriptEngine *getScriptEngine();
    static void setScriptEngine(ScriptEngine *engine);
    static void setWorldLoadRequestHandler(WorldLoadRequestHandler handler);
    static void requestWorldLoad(AssetId worldAssetId);
    // Request to quit the game (the script quit). The editor intercepts it with a handler (Stop
    // instead of closing the application); without a handler (runtime) — the application closes.
    using QuitRequestHandler = std::function<void()>;
    static void setQuitRequestHandler(QuitRequestHandler handler);
    static void requestQuit();
    // Save directory: set by the host (editor — <project>/.Panda/SaveData, runtime — an OS path).
    static void setPersistentDataPath(const path_t &path);
    static const path_t &getPersistentDataPath();
    // Assets mutated by the simulation (the script ABI, dirty is not set): on Stop the editor
    // takes the set and reloads them from disk — side effects of Play do not survive the exit.
    // In a standalone runtime nobody takes the set — it only collects unique ids, which is cheap.
    static void markRuntimeMutatedAsset(AssetId id);
    static std::unordered_set<AssetId> takeRuntimeMutatedAssets();

private:
    static World *s_currentWorld;
    static AssetHandler *s_assetHandler;
    static ScriptEngine *s_scriptEngine;
    static WorldLoadRequestHandler s_worldLoadRequestHandler;
    static QuitRequestHandler s_quitRequestHandler;
    static path_t s_persistentDataPath;
    static std::unordered_set<AssetId> s_runtimeMutatedAssets;
};

} // namespace Panda
