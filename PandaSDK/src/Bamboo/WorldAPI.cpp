#include "Bamboo/WorldAPI.hpp"
#include "Bamboo/Internal/MainThread.hpp"
#include "Panda/OuterScriptHook.hpp"

namespace Bamboo {

namespace WorldAPI {
    void load(WorldHandle world) {
        if (!Internal::requireMainThreadApi("WorldAPI::load")) { return; }
        Panda::ExternalCalls::world_Load(world.id);
    }

    EntityHandle findByTag(const char *tag) {
        if (!Internal::requireMainThreadApi("WorldAPI::findByTag")) { return {}; }
        return Panda::ExternalCalls::world_FindByTag(tag);
    }

    std::vector<EntityHandle> findAllByTag(const char *tag) {
        if (!Internal::requireMainThreadApi("WorldAPI::findAllByTag")) { return {}; }
        const uint32_t count = Panda::ExternalCalls::world_FindAllByTag(tag, nullptr, 0);
        std::vector<Panda::Scripting::Handle> rawHandles(count);
        if (count > 0) { Panda::ExternalCalls::world_FindAllByTag(tag, rawHandles.data(), count); }

        std::vector<EntityHandle> entities;
        entities.reserve(count);
        for (Panda::Scripting::Handle handle : rawHandles) {
            entities.emplace_back(handle);
        }
        return entities;
    }

    EntityHandle createEntity(const char *tag) {
        if (!Internal::requireMainThreadApi("WorldAPI::createEntity")) { return {}; }
        return Panda::ExternalCalls::world_CreateEntity(tag);
    }

    void destroyEntity(EntityHandle entityHandle) {
        if (!Internal::requireMainThreadApi("WorldAPI::destroyEntity")) { return; }
        Panda::ExternalCalls::world_DestroyEntity(entityHandle.id);
    }
} // namespace WorldAPI

} // namespace Bamboo
