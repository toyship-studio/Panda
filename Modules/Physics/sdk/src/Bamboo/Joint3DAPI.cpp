#include "Bamboo/Joint3DAPI.hpp"
#include "Bamboo/Internal/MainThread.hpp"
#include "Bamboo/Internal/PhysicsTable.hpp"

namespace Bamboo {

namespace Joint3DAPI {
    bool getReaction(EntityHandle entity, Joint3DKind kind, Reaction &reaction) {
        reaction = Reaction{};
        if (!Internal::requireMainThreadApi("Joint3DAPI::getReaction")) { return false; }
        const auto *api = Internal::physicsTable();
        if (api == nullptr) { return false; }
        float force[3] = {0.f, 0.f, 0.f};
        float torque[3] = {0.f, 0.f, 0.f};
        const uint32_t live = api->joint3D_GetReaction(entity.id, static_cast<uint32_t>(kind), force, torque);
        reaction.force = {force[0], force[1], force[2]};
        reaction.torque = {torque[0], torque[1], torque[2]};
        return live != 0;
    }
} // namespace Joint3DAPI

} // namespace Bamboo
