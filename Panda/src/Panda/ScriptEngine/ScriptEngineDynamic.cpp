#include "Panda/ScriptEngine/ScriptEngine.hpp"
#include "Panda/ScriptEngine/InnerScriptHook.hpp"
#include "Panda/Packages/ModuleShadowCopy.hpp"

#include <Foundation/Foundation.hpp>
#include <dylib.hpp>
#include <stdexcept>

namespace Panda {

namespace {

    void unloadDynamicLibrary(void *library) {
        F_DELETE(Foundation::getAllocator(), static_cast<dylib *>(library));
    }

    std::string decoratedLibraryName(const std::string &name) {
        return dylib::filename_components::prefix + name + dylib::filename_components::suffix;
    }

} // namespace

std::filesystem::path ScriptEngine::libraryFilePath(const ScriptEngineConfig &config) {
    return config.dllPath / decoratedLibraryName(config.dllName);
}

bool ScriptEngine::reload(ScriptEngineConfig config) {
    terminate();
    freeScriptingResources();
    m_manifest = {};
    m_lib = nullptr;
    m_unloadLib = nullptr;
    m_isStatic = false;
    // Shadow copy (ModuleShadowCopy): the original in dllPath stays free, a rebuild of the
    // scripts does not run into the lock of the loaded file.
    std::filesystem::path moduleFile = libraryFilePath(config);
    if (!config.shadowCachePath.empty()) {
        std::filesystem::path shadowFile;
        if (!copyModuleToShadowCache(moduleFile, config.shadowCachePath, shadowFile)) {
            LOG_ERROR_EDITOR(
                "Scripts: can't copy %s from %s (library missing or locked)",
                moduleFile.filename().string().c_str(),
                config.dllPath.string().c_str()
            );
            return false;
        }
        moduleFile = shadowFile;
    }
    try {
        auto *dynamicLibrary = F_NEW(Foundation::getAllocator(), dylib)(moduleFile);
        m_lib = dynamicLibrary;
        m_unloadLib = unloadDynamicLibrary;
        auto loadPlugin =
            dynamicLibrary->get_function<const Panda::ScriptPluginApi *(const Panda::ScriptHostApi *)>("pandaLoadScriptBundle"
            );
        const ScriptPluginApi *pluginApi = loadPlugin(getScriptHostApi());
        if (!bindPluginApi(pluginApi)) { throw std::runtime_error("SCRIPT ENGINE: failed to bind plugin API"); }
    } catch (...) {
        if (m_lib && m_unloadLib) { m_unloadLib(m_lib); }
        m_lib = nullptr;
        m_unloadLib = nullptr;
        LOG_ERROR("SCRIPT ENGINE: Can't load script dynamic library %s", moduleFile.string().c_str());
        LOG_ERROR_EDITOR("Scripts: can't load dynamic library %s", moduleFile.string().c_str());
        return false;
    }
    return true;
}

} // namespace Panda
