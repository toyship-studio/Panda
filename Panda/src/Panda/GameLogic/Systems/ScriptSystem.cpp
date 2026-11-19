#include "Panda/GameLogic/Systems/ScriptSystem.hpp"

#include "Panda/GameLogic/GameContext.hpp"
#include "Panda/GameLogic/World.hpp"
#include "Panda/ScriptEngine/InnerScriptHook.hpp" // updateScriptAsync, cancelAndWaitScriptAsync

#include <algorithm>
#include <vector>

namespace Panda::ScriptSystem {

namespace {

    // Per-frame budget for applying background script tasks: otherwise a batch of completed tasks
    // causes a frame spike.
    constexpr uint32_t ASYNC_MAX_COMPLETIONS_PER_FRAME = 32;
    constexpr double ASYNC_MAX_MILLISECONDS_PER_FRAME = 2.0;

    // All script containers of live entities in class order (stable — within a
    // class it is the order of the registry entities).
    std::vector<ExternalScript *> inExecutionOrder(World &world) {
        std::vector<ExternalScript *> scripts;
        auto view = world.getAllEntitiesWith<ScriptListComponent>();
        for (auto entityHandle : view) {
            if (!world.isValidEntt(entityHandle)) { continue; }
            for (ExternalScript &container : view.get<ScriptListComponent>(entityHandle).scripts) {
                scripts.emplace_back(&container);
            }
        }
        std::stable_sort(scripts.begin(), scripts.end(), [](const ExternalScript *lhs, const ExternalScript *rhs) {
            return lhs->getOrder() < rhs->getOrder();
        });
        return scripts;
    }

} // namespace

namespace {

    // The script containers of one entity against the manifest of the loaded scripts.
    void syncEntity(Entity entity, const ScriptBundleManifest &manifest) {
        for (ExternalScript &container : entity.getComponent<ScriptListComponent>().scripts) {
            const ScriptClassManifest &clazz = manifest.getClass(container.getName().c_str());
            if (!clazz) {
                LOG_ERROR_EDITOR(
                    "The entity %s has the script %s, which the built scripts do not have. Build the scripts and reload.",
                    entity.getName().c_str(), container.getName().c_str()
                );
                continue;
            }
            container.setOrder(clazz.order);
            auto &fields = container.getFields();
            fields.erase(
                std::remove_if(
                    fields.begin(),
                    fields.end(),
                    [&](const ScriptField &field) {
                        const ScriptFieldManifest &fieldManifest = clazz.getField(field.name.c_str());
                        if (!fieldManifest) {
                            LOG_INFO_EDITOR(
                                "The script %s has no field %s any more: its value is dropped.",
                                container.getName().c_str(), field.name.c_str()
                            );
                            return true;
                        }
                        return false;
                    }
                ),
                fields.end()
            );
            for (auto &fieldManifest : clazz.fields) {
                if (!container.hasField(fieldManifest.name.c_str())) {
                    container.addField(ScriptField(
                        0, fieldManifest.handle, fieldManifest.name, fieldManifest.type,
                        defaultScriptFieldValue(fieldManifest.type, fieldManifest.list), fieldManifest.kind
                    ));
                    continue;
                }
                ScriptField &field = container.getField(fieldManifest.name.c_str());
                field.fieldId = fieldManifest.handle;
                // The class decides what the field is: a value of another type, or one value
                // where the class now has a list, cannot be kept. So is a reference to an asset
                // of another kind — the world file does not hold the kind, so a field that comes
                // from a file takes it here.
                const bool otherKind = !field.kind.empty() && field.kind != fieldManifest.kind;
                if (field.type != fieldManifest.type || field.isList() != fieldManifest.list || otherKind) {
                    field.type = fieldManifest.type;
                    field.value = defaultScriptFieldValue(fieldManifest.type, fieldManifest.list);
                }
                field.kind = fieldManifest.kind;
            }
        }
    }

    // The script instances of one entity, with the values of their fields.
    void instantiateEntity(Entity entity) {
        const UUID entityId = entity.getId();
        for (ExternalScript &container : entity.getComponent<ScriptListComponent>().scripts) {
            const Handle scriptInstanceId = ExternalCalls::instantiateScript(entityId, container.getName().c_str());
            if (!scriptInstanceId) {
                LOG_ERROR_EDITOR("The script %s could not be created: the built scripts have no class of that name. Build the scripts and reload.", container.getName().c_str());
                continue;
            }
            container.bind(entityId, scriptInstanceId);
            for (ScriptField &field : container.getFields()) { container.applyField(field); }
        }
    }

    // The containers of the given entities in class order.
    std::vector<ExternalScript *> inExecutionOrder(const std::vector<Entity> &entities) {
        std::vector<ExternalScript *> scripts;
        for (Entity entity : entities) {
            if (!entity.isValid()) { continue; }
            for (ExternalScript &container : entity.getComponent<ScriptListComponent>().scripts) {
                scripts.emplace_back(&container);
            }
        }
        std::stable_sort(scripts.begin(), scripts.end(), [](const ExternalScript *lhs, const ExternalScript *rhs) {
            return lhs->getOrder() < rhs->getOrder();
        });
        return scripts;
    }

} // namespace

void syncWithManifest(World &world) {
    ScriptEngine *scriptEngine = GameContext::getScriptEngine();
    if (!scriptEngine || !scriptEngine->isLoaded()) { return; }
    auto view = world.getAllEntitiesWith<ScriptListComponent>();
    const ScriptBundleManifest manifest = scriptEngine->getManifest();
    for (auto entityHandle : view) {
        if (!world.isValidEntt(entityHandle)) { continue; }
        syncEntity(world.entity(entityHandle), manifest);
    }
}

void instantiate(World &world) {
    ScriptEngine *scriptEngine = GameContext::getScriptEngine();
    if (!scriptEngine || !scriptEngine->isLoaded()) { return; }
    auto view = world.getAllEntitiesWith<ScriptListComponent>();
    for (auto entityHandle : view) {
        if (!world.isValidEntt(entityHandle)) { continue; }
        instantiateEntity(world.entity(entityHandle));
    }
}

void instantiate(World &, const std::vector<Entity> &entities) {
    ScriptEngine *scriptEngine = GameContext::getScriptEngine();
    if (!scriptEngine || !scriptEngine->isLoaded()) { return; }
    // The entities came from elsewhere — a prefab, a copy made in another build of the scripts:
    // their containers follow the classes that are loaded now, like those of a world at its load.
    const ScriptBundleManifest manifest = scriptEngine->getManifest();
    for (Entity entity : entities) {
        if (!entity.isValid() || entity.getComponent<ScriptListComponent>().scripts.empty()) { continue; }
        syncEntity(entity, manifest);
        instantiateEntity(entity);
    }
}

void start(const std::vector<Entity> &entities) {
    for (ExternalScript *script : inExecutionOrder(entities)) {
        if (script->isBound()) { script->invokeStart(); }
    }
}

void shutdown(World &world) {
    // Workers and deferred completions hold references to script instances, so
    // they are stopped before the instances are removed.
    cancelAndWaitScriptAsync();
    if (ExternalCalls::clear) { ExternalCalls::clear(); } // a world may run with no scripts loaded
    auto view = world.getAllEntitiesWith<ScriptListComponent>();
    for (auto entityHandle : view) {
        if (!world.isValidEntt(entityHandle)) { continue; }
        for (ExternalScript &container : view.get<ScriptListComponent>(entityHandle).scripts) {
            container.unbind();
        }
    }
}

void start(World &world) {
    for (ExternalScript *script : inExecutionOrder(world)) { script->invokeStart(); }
}

void fixedUpdate(World &world, double stepTime) {
    for (ExternalScript *script : inExecutionOrder(world)) { script->invokeFixedUpdate(stepTime); }
}

void update(World &world, double deltaTime) {
    updateScriptAsync(ASYNC_MAX_COMPLETIONS_PER_FRAME, ASYNC_MAX_MILLISECONDS_PER_FRAME);
    for (ExternalScript *script : inExecutionOrder(world)) { script->invokeUpdate(deltaTime); }
}

void lateUpdate(World &world, double deltaTime) {
    for (ExternalScript *script : inExecutionOrder(world)) { script->invokeLateUpdate(deltaTime); }
}

void applicationFocus(World &world, bool focused) {
    if (!world.isRunning()) { return; }
    for (ExternalScript *script : inExecutionOrder(world)) { script->invokeApplicationFocus(focused); }
}

void applicationPause(World &world, bool paused) {
    if (!world.isRunning()) { return; }
    for (ExternalScript *script : inExecutionOrder(world)) { script->invokeApplicationPause(paused); }
}

} // namespace Panda::ScriptSystem
