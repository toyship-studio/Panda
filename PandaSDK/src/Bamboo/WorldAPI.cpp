#include "Bamboo/WorldAPI.hpp"
#include "Bamboo/Internal/MainThread.hpp"
#include "Panda/OuterScriptHook.hpp"

namespace Bamboo {

namespace WorldAPI {
    void load(WorldHandle world) {
        if (!Internal::requireMainThreadApi("WorldAPI::load")) { return; }
        Panda::ExternalCalls::world_Load(world.id());
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

    namespace {
        EntityHandle instantiateAt(EntityHandle original, EntityHandle parent, const Vec3 *position, const Quat *rotation) {
            if (!Internal::requireMainThreadApi("WorldAPI::instantiate")) { return {}; }
            if (Panda::ExternalCalls::world_Instantiate == nullptr) { return {}; }
            // The ABI takes the quaternion as the transform setters do: w first.
            const float place[3] = {position ? position->x : 0.f, position ? position->y : 0.f, position ? position->z : 0.f};
            const float turn[4] = {rotation ? rotation->w : 1.f, rotation ? rotation->x : 0.f, rotation ? rotation->y : 0.f,
                                   rotation ? rotation->z : 0.f};
            return Panda::ExternalCalls::world_Instantiate(
                original.id, parent.id, position ? place : nullptr, rotation ? turn : nullptr
            );
        }
    } // namespace

    EntityHandle instantiate(EntityHandle original) {
        return instantiateAt(original, EntityHandle(), nullptr, nullptr);
    }

    EntityHandle instantiate(EntityHandle original, EntityHandle parent) {
        return instantiateAt(original, parent, nullptr, nullptr);
    }

    EntityHandle instantiate(EntityHandle original, Vec3 position) {
        return instantiateAt(original, EntityHandle(), &position, nullptr);
    }

    EntityHandle instantiate(EntityHandle original, Vec3 position, Quat rotation) {
        return instantiateAt(original, EntityHandle(), &position, &rotation);
    }

    EntityHandle instantiate(EntityHandle original, EntityHandle parent, Vec3 position) {
        return instantiateAt(original, parent, &position, nullptr);
    }

    EntityHandle instantiate(EntityHandle original, EntityHandle parent, Vec3 position, Quat rotation) {
        return instantiateAt(original, parent, &position, &rotation);
    }

    void destroyEntity(EntityHandle entityHandle) {
        if (!Internal::requireMainThreadApi("WorldAPI::destroyEntity")) { return; }
        Panda::ExternalCalls::world_DestroyEntity(entityHandle.id);
    }
} // namespace WorldAPI

} // namespace Bamboo
