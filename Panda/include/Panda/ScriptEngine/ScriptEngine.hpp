#pragma once

#include <Panda/ScriptEngine/Manifest/ScriptBundleManifest.hpp>
#include <Panda/ScriptEngine/ABI/ScriptApi.hpp>
#include <filesystem>

namespace Panda {

struct ScriptEngineConfig {
    std::filesystem::path dllPath;
    std::string dllName;
    // Where to copy the library before loading (shadow copy). The copy is what gets
    // loaded, so the original in dllPath is never locked and the scripts can be
    // rebuilt while the engine is running. Empty — load the original.
    std::filesystem::path shadowCachePath;
};

class ScriptEngine {
public:
    ScriptEngine();
    ~ScriptEngine();
    bool reload(ScriptEngineConfig config);
    // Full path of the config's library file (platform name prefix/suffix) — for the
    // editor's mtime synchronization of Reload, without duplicating the name decoration.
    static std::filesystem::path libraryFilePath(const ScriptEngineConfig &config);
    bool loadStatic(LoadScriptPluginFn entrypoint);
    bool isLoaded();
    ScriptBundleManifest getManifest();
    void terminate();

private:
    bool bindPluginApi(const ScriptPluginApi *pluginApi);

    ScriptBundleManifest m_manifest;
    void *m_lib;
    void (*m_unloadLib)(void *);
    bool m_isStatic;
};

} // namespace Panda
