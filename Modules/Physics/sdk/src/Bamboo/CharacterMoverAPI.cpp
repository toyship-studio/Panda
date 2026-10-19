#include "Bamboo/CharacterMoverAPI.hpp"
#include "Bamboo/Internal/MainThread.hpp"
#include "Bamboo/Internal/PhysicsTable.hpp"

namespace Bamboo {

namespace CharacterMoverAPI {
    MoveResult move(EntityHandle entity, Vec3 translation, Vec3 velocity, float pushForce) {
        MoveResult result;
        result.velocity = velocity;
        if (!Internal::requireMainThreadApi("CharacterMoverAPI::move")) { return result; }
        const Panda::Scripting::PhysicsHostApi *api = Internal::physicsTable();
        if (api == nullptr) { return result; }
        uint32_t grounded = 0;
        api->character_Move(
            entity.id,
            translation.x,
            translation.y,
            translation.z,
            pushForce,
            &result.velocity.x,
            &result.velocity.y,
            &result.velocity.z,
            &result.translation.x,
            &result.translation.y,
            &result.translation.z,
            &grounded
        );
        result.grounded = grounded != 0;
        return result;
    }
} // namespace CharacterMoverAPI

} // namespace Bamboo
