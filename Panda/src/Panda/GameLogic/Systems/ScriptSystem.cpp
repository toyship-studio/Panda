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

    ScriptFieldValue defaultValueFor(ScriptFieldType type) {
        switch (type) {
            case ScriptFieldType::INTEGER:
                return int(0);
            case ScriptFieldType::FLOAT:
                return float(0.f);
            case ScriptFieldType::ENTITY:
            case ScriptFieldType::TEXTURE:
            case ScriptFieldType::MATERIAL:
            case ScriptFieldType::WORLD:
            case ScriptFieldType::FONT:
                return UUID(0);
            case ScriptFieldType::UNKNOWN:
                break;
        }
        return int(0);
    }

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

void syncWithManifest(World &world) {
    ScriptEngine *scriptEngine = GameContext::getScriptEngine();
    if (!scriptEngine || !scriptEngine->isLoaded()) { return; }
    auto view = world.getAllEntitiesWith<ScriptListComponent>();
    auto manifest = scriptEngine->getManifest();
    for (auto entityHandle : view) {
        if (!world.isValidEntt(entityHandle)) { continue; }
        auto &component = view.get<ScriptListComponent>(entityHandle);
        for (ExternalScript &container : component.scripts) {
            const ScriptClassManifest &clazz = manifest.getClass(container.getName().c_str());
            if (!clazz) {
                Entity entity = world.entity(entityHandle);
                LOG_ERROR_EDITOR(
                    "SCRIPT CLASS MANIFEST %s NOT FOUND.", container.getName().c_str(), entity.getName().c_str()
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
                                "SCRIPT %s FIELD %s NOT FOUND.", container.getName().c_str(), field.name.c_str()
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
                        defaultValueFor(fieldManifest.type)
                    ));
                    continue;
                }
                ScriptField &field = container.getField(fieldManifest.name.c_str());
                field.fieldId = fieldManifest.handle;
                if (field.type != fieldManifest.type) {
                    field.type = fieldManifest.type;
                    field.value = defaultValueFor(fieldManifest.type);
                }
            }
        }
    }
}

void instantiate(World &world) {
    ScriptEngine *scriptEngine = GameContext::getScriptEngine();
    if (!scriptEngine || !scriptEngine->isLoaded()) { return; }
    auto view = world.getAllEntitiesWith<ScriptListComponent>();
    for (auto entityHandle : view) {
        if (!world.isValidEntt(entityHandle)) { continue; }
        auto &component = view.get<ScriptListComponent>(entityHandle);
        const UUID entityId = world.entity(entityHandle).getId();
        for (ExternalScript &container : component.scripts) {
            const Handle scriptInstanceId = ExternalCalls::instantiateScript(entityId, container.getName().c_str());
            if (!scriptInstanceId) {
                LOG_ERROR_EDITOR("CANNOT INSTANTIATE SCRIPT %s", container.getName().c_str());
                continue;
            }
            container.bind(entityId, scriptInstanceId);
            for (ScriptField &field : container.getFields()) { container.applyField(field); }
        }
    }
}

void shutdown(World &world) {
    // Workers and deferred completions hold references to script instances, so
    // they are stopped before the instances are removed.
    cancelAndWaitScriptAsync();
    ExternalCalls::clear();
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

} // namespace Panda::ScriptSystem
