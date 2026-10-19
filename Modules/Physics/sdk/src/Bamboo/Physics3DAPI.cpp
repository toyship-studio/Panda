#include "Bamboo/Physics3DAPI.hpp"
#include "Bamboo/Internal/MainThread.hpp"
#include "Bamboo/Internal/PhysicsTable.hpp"

namespace Bamboo {

namespace Physics3DAPI {
    bool raycast(Vec3 origin, Vec3 direction, float maxDistance, RaycastHit &hit, EntityHandle ignored) {
        if (!Internal::requireMainThreadApi("Physics3DAPI::raycast")) { return false; }
        const Panda::Scripting::PhysicsHostApi *api = Internal::physicsTable();
        if (api == nullptr) { return false; }
        Panda::Scripting::RaycastHit3DData data;
        const uint32_t found = api->physics3D_Raycast(
            origin.x, origin.y, origin.z, direction.x, direction.y, direction.z, maxDistance, ignored.id, &data
        );
        if (found == 0) { return false; }
        hit.entity = EntityHandle(data.entityHandle);
        hit.point = Vec3(data.pointX, data.pointY, data.pointZ);
        hit.normal = Vec3(data.normalX, data.normalY, data.normalZ);
        hit.distance = data.distance;
        hit.friction = data.friction;
        return true;
    }
} // namespace Physics3DAPI

} // namespace Bamboo
