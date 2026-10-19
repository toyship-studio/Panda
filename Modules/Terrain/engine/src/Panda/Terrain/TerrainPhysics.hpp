#pragma once

#include "Panda/GameLogic/Entity.hpp"

#include <box3d/box3d.h>

#include <vector>

namespace Panda {

class World;
class Physics3D;

// The terrain ↔ box3d bridge (the lifecycle hooks of the terrain world system, which runs
// after the "physics" system of the Physics module). Terrain2D → a static body with a
// heightfield shape, Terrain3D → a static body with per-chunk mesh shapes from the same MC
// geometry as the rendering. The bodies live in the physics world of Physics3D
// (World::context<Physics3D>; they are registered for the entity — contact events reach the
// scripts), their data is in the per-world state of the system
// (World::context<TerrainPhysics::State>). A terrain entity with a body is tracked by the world
// (World::trackEntity): a transform set on it or above it rebuilds the body where the transform
// puts it.
namespace TerrainPhysics {

    struct RuntimeBody {
        UUID entityId = 0;
        b3BodyId body = b3_nullBodyId;
        std::vector<b3ShapeId> chunkShapes;       // by chunk index (null — no shape); 2d: [0]
        std::vector<b3MeshData *> chunkMeshDatas; // 3d: by chunk index (owned)
        b3HeightFieldData *heightField = nullptr; // 2d: owned (unit scale — the asset cache)
    };

    struct State {
        std::vector<RuntimeBody> bodies;
    };

    // Bodies of all terrain components of the world (physicsEnabled) — at simulation start.
    // Without the physics in the world context (the Physics module is not loaded) nothing is
    // created and every hook is a no-op.
    void start(World &world);
    // The bodies with their registration, the mesh/heightfield data and the state.
    void stop(World &world);
    // A terrain entity leaves the running simulation (it is destroyed, or deleted in the editor):
    // its body, the registration of the body and its data go with it. It comes back (the
    // deletion is undone): the body is created again.
    void entityRemoved(World &world, Entity entity);
    void entityRestored(World &world, Entity entity);
    // The transform of a terrain entity was set from outside (or of an entity above it): the
    // static body is rebuilt where the transform puts it (the shapes carry the scale).
    void transformChanged(World &world, Entity entity, bool scale);

    // Runtime edit of 3d terrain voxels (the range is in LOCAL voxels): re-create
    // the mesh shapes of the affected chunks and wake up the dynamic bodies in the region.
    void voxelsUpdated(World &world, Entity entity, int minX, int minY, int minZ, int maxX, int maxY, int maxZ);
    // Runtime edit of 2d terrain heights: re-create the heightfield shape as a whole and wake up
    // the bodies above the vertex rectangle.
    void heightsUpdated(World &world, Entity entity, uint32_t minX, uint32_t minZ, uint32_t maxX, uint32_t maxZ);

} // namespace TerrainPhysics

} // namespace Panda
