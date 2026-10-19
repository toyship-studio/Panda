#pragma once

#include "Base.hpp"

#include <optional>
#include <vector>

namespace Bamboo {

namespace WorldAPI {
    void load(WorldHandle world);
    EntityHandle findByTag(const char *tag);
    std::vector<EntityHandle> findAllByTag(const char *tag);
    EntityHandle createEntity(const char *tag);
    // The entity (with its children) is destroyed when the callbacks that are running now are
    // over: a script may destroy any entity, its own included, from any callback. Until then the
    // entity is still in the world.
    void destroyEntity(EntityHandle entity);
}; // namespace WorldAPI

} // namespace Bamboo
