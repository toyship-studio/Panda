#include "Project/Modules/ProjectModules.hpp"

#include <Foundation/Foundation.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/Packages/PackageLoader.hpp>
#include <PandaEditorKit/EditorExtensions.hpp>

#include <algorithm>
#include <functional>
#include <unordered_set>

namespace Panda {

bool ProjectModules::isEnabled(const std::vector<std::string> &disabled, const std::string &id) {
    return std::find(disabled.begin(), disabled.end(), id) == disabled.end();
}

std::vector<const BuiltinModule *> ProjectModules::resolve(const std::vector<std::string> &disabled) {
    const std::vector<BuiltinModule> &table = builtinModules();
    std::vector<const BuiltinModule *> resolved;
    std::unordered_set<std::string> visiting;
    // Depth first through the dependencies: a module gets into the list after them. A module is
    // dropped when it is disabled, or when one of its dependencies is dropped (missing from the
    // table, disabled, or in a cycle). The warnings go once per project open.
    std::function<bool(const BuiltinModule &)> visit = [&](const BuiltinModule &module) -> bool {
        for (const BuiltinModule *entry : resolved) {
            if (entry == &module) { return true; }
        }
        if (!isEnabled(disabled, module.id)) { return false; }
        if (!visiting.insert(module.id).second) {
            LOG_WARN_EDITOR("Module %s is in a dependency cycle; it is not loaded.", module.id);
            return false;
        }
        bool satisfied = true;
        for (const std::string &dependencyId : module.dependencies) {
            const BuiltinModule *dependency = nullptr;
            for (const BuiltinModule &entry : table) {
                if (dependencyId == entry.id) { dependency = &entry; }
            }
            if (dependency == nullptr) {
                LOG_WARN_EDITOR(
                    "Module %s needs %s, which this build does not have; it is not loaded.", module.id,
                    dependencyId.c_str()
                );
                satisfied = false;
            } else if (!visit(*dependency)) {
                LOG_WARN_EDITOR(
                    "Module %s needs %s, which is disabled in this project; it is not loaded.", module.id,
                    dependencyId.c_str()
                );
                satisfied = false;
            }
        }
        visiting.erase(module.id);
        if (satisfied) { resolved.push_back(&module); }
        return satisfied;
    };
    for (const BuiltinModule &module : table) { visit(module); }
    return resolved;
}

bool ProjectModules::restartNeeded(const std::vector<std::string> &disabled) {
    PackageLoader &loader = GameContext::getPackageLoader();
    const std::vector<const BuiltinModule *> enabled = resolve(disabled);
    for (const BuiltinModule &module : builtinModules()) {
        const bool wanted = std::find(enabled.begin(), enabled.end(), &module) != enabled.end();
        const bool registered = loader.find(module.id) != nullptr;
        if (wanted != registered) { return true; }
    }
    return false;
}

bool ProjectModules::loadNeeded(const std::vector<std::string> &disabled) {
    PackageLoader &loader = GameContext::getPackageLoader();
    for (const BuiltinModule *module : resolve(disabled)) {
        if (loader.find(module->id) == nullptr) { return true; }
    }
    return false;
}

int ProjectModules::load(const std::vector<std::string> &disabled) {
    PackageLoader &loader = GameContext::getPackageLoader();
    const std::vector<const BuiltinModule *> enabled = resolve(disabled);
    int loaded = 0;
    for (const BuiltinModule &module : builtinModules()) {
        const bool wanted = std::find(enabled.begin(), enabled.end(), &module) != enabled.end();
        if (!wanted && loader.find(module.id) != nullptr) {
            LOG_WARN_EDITOR(
                "Module %s is disabled in this project but already registered. Restart the editor to drop it.",
                module.id
            );
        }
    }
    for (const BuiltinModule *module : enabled) {
        if (loader.find(module->id) != nullptr) { continue; }
        if (loader.loadStatic(module->id, module->version, module->engine)) { loaded++; }
    }
    return loaded;
}

int ProjectModules::loadEditorParts(const std::vector<std::string> &disabled) {
    static std::unordered_set<std::string> s_registered; // editor parts are not unloaded
    int loaded = 0;
    for (const BuiltinModule *module : resolve(disabled)) {
        if (module->editor == nullptr || s_registered.count(module->id) > 0) { continue; }
        Editor::EditorExtensions &extensions = Editor::editorExtensions();
        extensions.componentInspectors.setGroup(module->name); // what its components are filed under
        module->editor(extensions);
        extensions.componentInspectors.setGroup({});
        s_registered.insert(module->id);
        loaded++;
    }
    return loaded;
}

std::vector<std::string> ProjectModules::enabledIds(const std::vector<std::string> &disabled) {
    std::vector<std::string> ids;
    for (const BuiltinModule *module : resolve(disabled)) { ids.emplace_back(module->id); }
    return ids;
}

} // namespace Panda
