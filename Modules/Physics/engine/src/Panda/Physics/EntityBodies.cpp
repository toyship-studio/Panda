#include "Panda/Physics/EntityBodies.hpp"
#include "Panda/GameLogic/World.hpp"

namespace Panda {

void EntityBodiesBase::bodyAdded(Entity entity) {
    entity.getWorld()->trackEntity(entity);
}

void EntityBodiesBase::bodyRemoved(Entity entity) {
    entity.getWorld()->untrackEntity(entity);
}

} // namespace Panda
