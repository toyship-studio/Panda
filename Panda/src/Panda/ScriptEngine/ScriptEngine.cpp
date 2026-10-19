#include "Panda/ScriptEngine/ScriptEngine.hpp"
#include "Panda/ScriptEngine/InnerScriptHook.hpp"

#include <Foundation/Logger.hpp>

namespace Panda {

ScriptEngine::ScriptEngine()
    : m_manifest()
    , m_lib(nullptr)
    , m_unloadLib(nullptr)
    , m_isStatic(false) {}

ScriptEngine::~ScriptEngine() {
    terminate();
}

void ScriptEngine::terminate() {
    // No worker of the module may be running by the time of clear()/the library unload.
    cancelAndWaitScriptAsync();
    if (m_isStatic) {
        if (ExternalCalls::clear) { ExternalCalls::clear(); }
        clearScriptPluginApi();
        m_lib = nullptr;
        m_unloadLib = nullptr;
        m_isStatic = false;
        m_manifest = {};
        return;
    }
    if (m_lib) {
        if (ExternalCalls::clear) { ExternalCalls::clear(); }
        clearScriptPluginApi();
        if (m_unloadLib) { m_unloadLib(m_lib); }
        m_lib = nullptr;
        m_unloadLib = nullptr;
    }
    m_manifest = {};
}

ScriptBundleManifest ScriptEngine::getManifest() {
    return m_manifest;
}

bool ScriptEngine::bindPluginApi(const ScriptPluginApi *pluginApi) {
    if (!pluginApi || pluginApi->abiVersion != Panda::Scripting::SCRIPT_ABI_VERSION) {
        LOG_ERROR("SCRIPT ENGINE: invalid scripting ABI");
        LOG_ERROR_EDITOR(
            "Scripts: ABI mismatch — engine v%u, assembly v%u (rebuild scripts)",
            Panda::Scripting::SCRIPT_ABI_VERSION,
            pluginApi ? pluginApi->abiVersion : 0u
        );
        return false;
    }
    bindScriptPluginApi(pluginApi);
    m_manifest = ScriptBundleManifest::fromInfo(ExternalCalls::getManifest());
    return true;
}

bool ScriptEngine::loadStatic(LoadScriptPluginFn entrypoint) {
    terminate();
    freeScriptingResources();
    m_manifest = {};
    m_lib = nullptr;
    m_unloadLib = nullptr;
    if (!entrypoint) {
        LOG_ERROR("SCRIPT ENGINE: static script entrypoint is null");
        return false;
    }
    m_isStatic = true;
    const ScriptPluginApi *pluginApi = entrypoint(getScriptHostApi());
    if (!bindPluginApi(pluginApi)) {
        if (ExternalCalls::clear) { ExternalCalls::clear(); }
        clearScriptPluginApi();
        m_isStatic = false;
        m_manifest = {};
        LOG_ERROR("SCRIPT ENGINE: Can't load static script plugin");
        return false;
    }
    return true;
}

bool ScriptEngine::isLoaded() {
    return m_lib || m_isStatic;
}

} // namespace Panda
