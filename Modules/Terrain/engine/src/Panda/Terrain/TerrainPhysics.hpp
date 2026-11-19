#pragma once

#include "Panda/GameLogic/Entity.hpp"

#include <box3d/box3d.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Panda {

class World;
class Physics3D;
struct MeshColliderCook; // the geometry a mesh collider shape points into (the Physics module)

// The terrain ↔ box3d bridge (the lifecycle hooks of the terrain world system, which runs
// after the "physics" system of the Physics module). Terrain2D → a static body with a
// heightfield shape, Terrain3D → a static body with per-chunk mesh shapes from the same MC
// geometry as the rendering. The bodies live in the physics world of Physics3D
// (World::context<Physics3D>; they are registered for the entity — contact events reach the
// scripts), their data is in the per-world state of the system
// (World::context<TerrainPhysics::State>). A terrain entity with a body is tracked by the world
// (World::trackEntity): a transform set on it or above it rebuilds the body where the transform
// puts it.
//
// The trees of a Terrain2D collide too, when their layer says what a tree is with a prefab that
// holds colliders: every tree of the layer gets the colliders of the prefab — of every entity
// of it, where it stands under the root — as shapes of the body of the terrain, at the place,
// the turn and the size of the tree. They are made when the world starts to run, and again
// where the relief changes under them; what touches a tree touches the terrain entity.
namespace TerrainPhysics {

    // The shapes of the trees of one cell of TerrainTrees, with the geometry the mesh shapes
    // among them point into.
    struct TreeCell {
        std::vector<b3ShapeId> shapes;
        std::vector<std::shared_ptr<MeshColliderCook>> cooks;
    };

    struct RuntimeBody {
        UUID entityId = 0;
        b3BodyId body = b3_nullBodyId;
        std::vector<b3ShapeId> chunkShapes;       // by chunk index (null — no shape); 2d: [0]
        std::vector<b3MeshData *> chunkMeshDatas; // 3d: by chunk index (owned)
        b3HeightFieldData *heightField = nullptr; // 2d: owned (unit scale — the asset cache)
        std::unordered_map<uint64_t, TreeCell> treeCells; // 2d: by cell (z << 32 | x)
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
