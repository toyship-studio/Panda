#include "Panda/Terrain/TerrainPhysics.hpp"
#include "Panda/Terrain/TerrainComponents.hpp"
#include "Panda/GameLogic/World.hpp"
#include "Panda/Physics/Box3DHandles.hpp"
#include "Panda/Physics/Physics3D.hpp"
#include "Panda/Renderer/Vertex.hpp"
#include "Panda/Terrain/Terrain2DAsset.hpp"
#include "Panda/Terrain/Terrain3DAsset.hpp"
#include "Panda/Terrain/TerrainMesher3D.hpp"

#include <algorithm>
#include <cfloat>
#include <vector>

namespace Panda {

namespace TerrainPhysics {

    namespace {

        b3Quat toB3Quat(const glm::quat &q) {
            b3Quat result;
            result.v = {q.x, q.y, q.z};
            result.s = q.w;
            return result;
        }

        b3ShapeDef terrainShapeDef() {
            b3ShapeDef shapeDef = b3DefaultShapeDef();
            shapeDef.baseMaterial.friction = 0.6f;
            shapeDef.baseMaterial.restitution = 0.f;
            // Terrain contact events reach the scripts (the body is in bodyEntityMap).
            shapeDef.enableContactEvents = true;
            return shapeDef;
        }

        b3BodyId createStaticBody(b3WorldId worldId, Entity entity) {
            auto transform = entity.calculateWorldSpaceTransform();
            b3BodyDef bodyDef = b3DefaultBodyDef();
            bodyDef.type = b3_staticBody;
            bodyDef.position = {
                transform.getPosition().x, transform.getPosition().y, transform.getPosition().z
            };
            bodyDef.rotation = toB3Quat(transform.getRotation());
            return b3CreateBody(worldId, &bodyDef);
        }

        void createHeightFieldShape(b3BodyId bodyId, Entity entity, Terrain2DAsset &asset,
                                    RuntimeBody &runtime) {
            const TerrainData2D &data = asset.getData();
            const glm::vec3 scale = glm::abs(entity.calculateWorldSpaceTransform().getScale());

            // b3CreateHeightField is several full passes over the map (height compression,
            // edge convexity flags): on large terrains this is the main freeze at game start.
            // The structure is cached IN THE ASSET and survives Play/Stop; a rebuild happens only
            // after a relief edit (the data version). The entity scale is baked into the
            // structure, so the cache is valid only for unit scale (the norm for a terrain) —
            // a scaled terrain goes the old way with the ownership in the runtime.
            const bool unitScale = glm::all(glm::lessThan(glm::abs(scale - glm::vec3(1.f)), glm::vec3(1e-4f)));
            const auto buildHeightField = [&]() -> b3HeightFieldData * {
                float minHeight = 0.f;
                float maxHeight = 0.f;
                asset.getHeightBounds(minHeight, maxHeight);
                b3HeightFieldDef def = {};
                // The heights are stored as uint16 (see TerrainData2D), while box3d expects float*.
                // It reads the array ONLY at creation and builds its own compressed structure,
                // so they are expanded into a temporary buffer for the duration of the call instead
                // of keeping a second copy of the relief in memory.
                std::vector<float> heightsFloat(data.heights.size());
                for (size_t i = 0; i < data.heights.size(); i++) {
                    heightsFloat[i] = TerrainData2D::decodeHeight(data.heights[i]);
                }
                def.heights = heightsFloat.data();
                def.materialIndices = nullptr; // all cells are material 0 (0xFF — holes, none here)
                def.scale = {data.cellSize * scale.x, scale.y, data.cellSize * scale.z};
                def.countX = static_cast<int>(data.vertexCountX());
                def.countZ = static_cast<int>(data.vertexCountZ());
                def.globalMinimumHeight = minHeight;
                def.globalMaximumHeight = std::max(maxHeight, minHeight + 0.01f);
                def.clockwiseWinding = false;
                return b3CreateHeightField(&def);
            };

            b3HeightFieldData *heightField = nullptr;
            if (unitScale) {
                uint64_t cachedVersion = 0;
                heightField = static_cast<b3HeightFieldData *>(asset.getPhysicsCache(cachedVersion));
                if (heightField == nullptr || cachedVersion != asset.getDataVersion()) {
                    heightField = buildHeightField();
                    if (heightField == nullptr) { return; }
                    asset.setPhysicsCache(
                        heightField,
                        [](void *cache) {
                            b3DestroyHeightField(static_cast<b3HeightFieldData *>(cache));
                        },
                        asset.getDataVersion()
                    );
                }
                runtime.heightField = nullptr; // the structure is owned by the asset cache
            } else {
                heightField = buildHeightField();
                if (heightField == nullptr) { return; }
                runtime.heightField = heightField;
            }

            b3ShapeDef shapeDef = terrainShapeDef();
            b3ShapeId shapeId = b3CreateHeightFieldShape(bodyId, &shapeDef, heightField);
            runtime.chunkShapes.assign(1, shapeId);
        }

        // Mesh shape of a single 3d terrain chunk; null — the chunk is empty (no shape is needed).
        b3ShapeId createChunkMeshShape(
            b3BodyId bodyId, const TerrainData3D &data, const b3Vec3 &meshScale, uint32_t chunkX,
            uint32_t chunkY, uint32_t chunkZ, b3MeshData *&outMeshData
        ) {
            outMeshData = nullptr;
            // Physics consumes the same MC geometry as the rendering: buildChunkMesh, the positions
            // are taken from the vertices.
            MeshData meshData = TerrainMesher3D::buildChunkMesh(data, chunkX, chunkY, chunkZ);
            const uint32_t vertexCount =
                meshData.vertexBufferSize / static_cast<uint32_t>(sizeof(Vertex));
            const uint32_t triangleCount = meshData.indicesCount / 3;
            if (vertexCount < 3 || triangleCount == 0) {
                meshData.vertexBuffer.release();
                meshData.indices.release();
                return b3_nullShapeId;
            }
            const Vertex *vertices = static_cast<const Vertex *>(meshData.vertexBuffer.data);
            std::vector<b3Vec3> positions(vertexCount);
            for (uint32_t i = 0; i < vertexCount; i++) {
                positions[i] = {vertices[i].pos.x, vertices[i].pos.y, vertices[i].pos.z};
            }
            std::vector<int32_t> indices(meshData.indicesCount);
            const uint32_t *sourceIndices = static_cast<const uint32_t *>(meshData.indices.data);
            for (uint32_t i = 0; i < meshData.indicesCount; i++) {
                indices[i] = static_cast<int32_t>(sourceIndices[i]);
            }
            meshData.vertexBuffer.release();
            meshData.indices.release();

            b3MeshDef meshDef = {};
            meshDef.vertices = positions.data();
            meshDef.indices = indices.data();
            meshDef.materialIndices = nullptr;
            meshDef.vertexCount = static_cast<int>(vertexCount);
            meshDef.triangleCount = static_cast<int>(triangleCount);
            // MC does not share vertices between triangles — they are welded for the edge
            // information (smooth contacts without snagging on internal edges).
            meshDef.weldVertices = true;
            meshDef.weldTolerance = 1e-4f;
            meshDef.useMedianSplit = true; // grid structure — a faster BVH build
            meshDef.identifyEdges = true;
            b3MeshData *mesh = b3CreateMesh(&meshDef, nullptr, 0);
            if (mesh == nullptr) { return b3_nullShapeId; }

            b3ShapeDef shapeDef = terrainShapeDef();
            b3ShapeId shapeId = b3CreateMeshShape(bodyId, &shapeDef, mesh, meshScale);
            outMeshData = mesh;
            return shapeId;
        }

        void createVoxelMeshShapes(b3BodyId bodyId, Entity entity, Terrain3DAsset &asset,
                                   RuntimeBody &runtime) {
            const TerrainData3D &data = asset.getData();
            const glm::vec3 scale = glm::abs(entity.calculateWorldSpaceTransform().getScale());
            const b3Vec3 meshScale = {scale.x, scale.y, scale.z};
            const size_t chunkCount =
                static_cast<size_t>(data.chunksX()) * data.chunksY() * data.chunksZ();
            runtime.chunkShapes.assign(chunkCount, b3_nullShapeId);
            runtime.chunkMeshDatas.assign(chunkCount, nullptr);

            for (uint32_t chunkY = 0; chunkY < data.chunksY(); chunkY++) {
                for (uint32_t chunkZ = 0; chunkZ < data.chunksZ(); chunkZ++) {
                    for (uint32_t chunkX = 0; chunkX < data.chunksX(); chunkX++) {
                        const size_t index =
                            (static_cast<size_t>(chunkY) * data.chunksZ() + chunkZ) * data.chunksX() +
                            chunkX;
                        b3MeshData *meshData = nullptr;
                        runtime.chunkShapes[index] =
                            createChunkMeshShape(bodyId, data, meshScale, chunkX, chunkY, chunkZ, meshData);
                        runtime.chunkMeshDatas[index] = meshData;
                    }
                }
            }
        }

        // Resolve of the component reference: .get() filters out both an unloaded asset and a
        // foreign type.
        Terrain2DAsset *resolveTerrain2D(const AssetRef<Asset> &terrainRef) {
            if (!terrainRef) { return nullptr; }
            Terrain2DAsset *asset = terrainRef.as<Terrain2DAsset>().get();
            return asset != nullptr && asset->getData().isValid() ? asset : nullptr;
        }

        Terrain3DAsset *resolveTerrain3D(const AssetRef<Asset> &terrainRef) {
            if (!terrainRef) { return nullptr; }
            Terrain3DAsset *asset = terrainRef.as<Terrain3DAsset>().get();
            return asset != nullptr && asset->getData().isValid() ? asset : nullptr;
        }

        // Wake up the dynamic bodies intersecting the world AABB (a terrain edit under them).
        void wakeBodiesInAABB(b3WorldId worldId, const glm::vec3 &minPoint, const glm::vec3 &maxPoint) {
            b3AABB aabb;
            aabb.lowerBound = {minPoint.x, minPoint.y, minPoint.z};
            aabb.upperBound = {maxPoint.x, maxPoint.y, maxPoint.z};
            auto wake = [](b3ShapeId shapeId, void *) -> bool {
                b3BodyId body = b3Shape_GetBody(shapeId);
                if (b3Body_GetType(body) == b3_dynamicBody) { b3Body_SetAwake(body, true); }
                return true;
            };
            b3World_OverlapAABB(worldId, aabb, b3DefaultQueryFilter(), wake, nullptr);
        }

        // Conservative world AABB of a terrain local box (8 corners through the transform).
        void localBoxToWorldAABB(
            const glm::mat4 &transform, const glm::vec3 &localMin, const glm::vec3 &localMax,
            glm::vec3 &outMin, glm::vec3 &outMax
        ) {
            outMin = glm::vec3(FLT_MAX);
            outMax = glm::vec3(-FLT_MAX);
            for (int corner = 0; corner < 8; corner++) {
                const glm::vec3 local(
                    (corner & 1) ? localMax.x : localMin.x, (corner & 2) ? localMax.y : localMin.y,
                    (corner & 4) ? localMax.z : localMin.z
                );
                const glm::vec3 world = glm::vec3(transform * glm::vec4(local, 1.f));
                outMin = glm::min(outMin, world);
                outMax = glm::max(outMax, world);
            }
        }

        // The mesh and heightfield data a body owns. The shapes that refer to it must be gone.
        void releaseData(RuntimeBody &runtime) {
            for (b3MeshData *mesh : runtime.chunkMeshDatas) {
                if (mesh != nullptr) { b3DestroyMesh(mesh); }
            }
            runtime.chunkMeshDatas.clear();
            if (runtime.heightField != nullptr) {
                b3DestroyHeightField(runtime.heightField);
                runtime.heightField = nullptr;
            }
        }

        RuntimeBody *bodyOf(State *state, Entity entity) {
            if (state == nullptr) { return nullptr; }
            for (RuntimeBody &body : state->bodies) {
                if (body.entityId == entity.getId()) { return &body; }
            }
            return nullptr;
        }

        // The static body of a terrain entity with its shapes. Nothing is created for an entity
        // without a terrain with physics, and for one that has its body already.
        void createBody(World &world, Physics3D &physics, Entity entity) {
            State &state = world.context<State>();
            if (bodyOf(&state, entity) != nullptr) { return; }
            const b3WorldId worldId = intToB3WorldId(physics.worldHandle());
            RuntimeBody runtime;
            runtime.entityId = entity.getId();
            if (entity.hasComponent<Terrain2DComponent>()) {
                Terrain2DComponent &component = entity.getComponent<Terrain2DComponent>();
                Terrain2DAsset *asset = component.physicsEnabled ? resolveTerrain2D(component.terrain) : nullptr;
                if (asset == nullptr) { return; }
                runtime.body = createStaticBody(worldId, entity);
                createHeightFieldShape(runtime.body, entity, *asset, runtime);
            } else if (entity.hasComponent<Terrain3DComponent>()) {
                Terrain3DComponent &component = entity.getComponent<Terrain3DComponent>();
                Terrain3DAsset *asset = component.physicsEnabled ? resolveTerrain3D(component.terrain) : nullptr;
                if (asset == nullptr) { return; }
                runtime.body = createStaticBody(worldId, entity);
                createVoxelMeshShapes(runtime.body, entity, *asset, runtime);
            } else {
                return;
            }
            physics.registerBody(runtime.body.index1, entity);
            world.trackEntity(entity); // a transform set above the terrain reaches its body
            state.bodies.push_back(std::move(runtime));
        }

        // The body of a terrain entity with its registration, its data and the tracking.
        void destroyBody(World &world, Physics3D &physics, Entity entity) {
            State *state = world.findContext<State>();
            if (state == nullptr) { return; }
            const UUID entityId = entity.getId();
            for (auto it = state->bodies.begin(); it != state->bodies.end();) {
                if (it->entityId != entityId) {
                    ++it;
                    continue;
                }
                physics.unregisterBody(it->body.index1);
                b3DestroyBody(it->body); // with its shapes; what rested on the terrain is woken up
                releaseData(*it);
                world.untrackEntity(entity);
                it = state->bodies.erase(it);
            }
        }

    } // namespace

    void start(World &world) {
        Physics3D *physics = world.findContext<Physics3D>();
        if (physics == nullptr) { return; } // no physics module: terrains have no bodies
        world.context<State>();
        world.getAllEntitiesWith<IdComponent, Terrain2DComponent>().each(
            [&](IdComponent &idComponent, Terrain2DComponent &) {
                Entity entity = world.getById(idComponent.id);
                if (entity.isValid()) { createBody(world, *physics, entity); }
            }
        );
        world.getAllEntitiesWith<IdComponent, Terrain3DComponent>().each(
            [&](IdComponent &idComponent, Terrain3DComponent &) {
                Entity entity = world.getById(idComponent.id);
                if (entity.isValid()) { createBody(world, *physics, entity); }
            }
        );
    }

    void stop(World &world) {
        State *state = world.findContext<State>();
        if (state == nullptr) { return; }
        // The physics world is still alive (the terrain stops before the physics): the bodies
        // go with their registration, and the tracking count comes to nothing.
        Physics3D *physics = world.findContext<Physics3D>();
        for (RuntimeBody &body : state->bodies) {
            if (physics != nullptr) {
                physics->unregisterBody(body.body.index1);
                b3DestroyBody(body.body);
            }
            releaseData(body);
            world.untrackEntity(world.getById(body.entityId));
        }
        world.eraseContext<State>();
    }

    void entityRemoved(World &world, Entity entity) {
        if (Physics3D *physics = world.findContext<Physics3D>()) { destroyBody(world, *physics, entity); }
    }

    void entityRestored(World &world, Entity entity) {
        Physics3D *physics = world.findContext<Physics3D>();
        if (physics != nullptr && entity.isValid()) { createBody(world, *physics, entity); }
    }

    void transformChanged(World &world, Entity entity, bool) {
        // A static body: rebuilt where the transform puts it now (the shapes carry the scale, so
        // a move and a scale edit take the same way; terrains are not moved every frame).
        Physics3D *physics = world.findContext<Physics3D>();
        if (physics == nullptr || bodyOf(world.findContext<State>(), entity) == nullptr) { return; }
        destroyBody(world, *physics, entity);
        createBody(world, *physics, entity);
    }

    void voxelsUpdated(World &world, Entity entity, int minX, int minY, int minZ, int maxX, int maxY, int maxZ) {
        Physics3D *physicsPtr = world.findContext<Physics3D>();
        if (physicsPtr == nullptr || !physicsPtr->isInitialized()) { return; }
        Physics3D &physics = *physicsPtr;
        RuntimeBody *body = bodyOf(world.findContext<State>(), entity);
        if (body == nullptr || !entity.hasComponent<Terrain3DComponent>()) { return; }
        Terrain3DAsset *asset = entity.getComponent<Terrain3DComponent>().terrain.as<Terrain3DAsset>().get();
        if (asset == nullptr) { return; }
        const TerrainData3D &data = asset->getData();
        if (!data.isValid()) { return; }
        const size_t chunkCount = static_cast<size_t>(data.chunksX()) * data.chunksY() * data.chunksZ();
        if (body->chunkShapes.size() != chunkCount) { return; } // asset resize at runtime is not supported

        const b3WorldId worldId = intToB3WorldId(physics.worldHandle());
        const glm::vec3 scale = glm::abs(entity.calculateWorldSpaceTransform().getScale());
        const b3Vec3 meshScale = {scale.x, scale.y, scale.z};

        // A voxel affects the field within a margin of 2 (density + gradient) — the same chunks
        // as the rendering.
        constexpr int SPREAD = 2;
        const int chunkCells = static_cast<int>(TerrainData3D::CHUNK_CELLS);
        const auto chunkOf = [chunkCells](int value, int chunks) {
            return std::clamp(value / chunkCells, 0, chunks - 1);
        };
        const int chunksX = static_cast<int>(data.chunksX());
        const int chunksY = static_cast<int>(data.chunksY());
        const int chunksZ = static_cast<int>(data.chunksZ());
        const int minChunkX = chunkOf(minX - SPREAD, chunksX);
        const int minChunkY = chunkOf(minY - SPREAD, chunksY);
        const int minChunkZ = chunkOf(minZ - SPREAD, chunksZ);
        const int maxChunkX = chunkOf(maxX + SPREAD, chunksX);
        const int maxChunkY = chunkOf(maxY + SPREAD, chunksY);
        const int maxChunkZ = chunkOf(maxZ + SPREAD, chunksZ);

        for (int chunkY = minChunkY; chunkY <= maxChunkY; chunkY++) {
            for (int chunkZ = minChunkZ; chunkZ <= maxChunkZ; chunkZ++) {
                for (int chunkX = minChunkX; chunkX <= maxChunkX; chunkX++) {
                    const size_t index =
                        (static_cast<size_t>(chunkY) * chunksZ + chunkZ) * chunksX + chunkX;
                    if (B3_IS_NON_NULL(body->chunkShapes[index])) {
                        b3DestroyShape(body->chunkShapes[index], false);
                        body->chunkShapes[index] = b3_nullShapeId;
                    }
                    if (body->chunkMeshDatas[index] != nullptr) {
                        b3DestroyMesh(body->chunkMeshDatas[index]);
                        body->chunkMeshDatas[index] = nullptr;
                    }
                    b3MeshData *meshData = nullptr;
                    body->chunkShapes[index] = createChunkMeshShape(
                        body->body, data, meshScale, static_cast<uint32_t>(chunkX),
                        static_cast<uint32_t>(chunkY), static_cast<uint32_t>(chunkZ), meshData
                    );
                    body->chunkMeshDatas[index] = meshData;
                }
            }
        }

        // Wake up the bodies above the edit (sleeping ones will not notice the disappearance of
        // the support).
        const glm::mat4 transform = entity.calculateWorldSpaceTransform().getLocalTransform();
        const glm::vec3 localMin(
            (minX - SPREAD) * data.cellSize, (minY - SPREAD) * data.cellSize,
            (minZ - SPREAD) * data.cellSize
        );
        const glm::vec3 localMax(
            (maxX + 1 + SPREAD) * data.cellSize, (maxY + 1 + SPREAD) * data.cellSize,
            (maxZ + 1 + SPREAD) * data.cellSize
        );
        glm::vec3 worldMin, worldMax;
        localBoxToWorldAABB(transform, localMin, localMax, worldMin, worldMax);
        wakeBodiesInAABB(worldId, worldMin - glm::vec3(0.5f), worldMax + glm::vec3(0.5f));
    }

    void heightsUpdated(World &world, Entity entity, uint32_t minX, uint32_t minZ, uint32_t maxX, uint32_t maxZ) {
        Physics3D *physicsPtr = world.findContext<Physics3D>();
        if (physicsPtr == nullptr || !physicsPtr->isInitialized()) { return; }
        Physics3D &physics = *physicsPtr;
        RuntimeBody *body = bodyOf(world.findContext<State>(), entity);
        if (body == nullptr || !entity.hasComponent<Terrain2DComponent>()) { return; }
        Terrain2DAsset *asset = entity.getComponent<Terrain2DComponent>().terrain.as<Terrain2DAsset>().get();
        if (asset == nullptr) { return; }
        const TerrainData2D &data = asset->getData();
        if (!data.isValid()) { return; }
        const b3WorldId worldId = intToB3WorldId(physics.worldHandle());

        // The heightfield is re-created as a whole (box3d stores it compressed, there are no
        // point edits).
        if (!body->chunkShapes.empty() && B3_IS_NON_NULL(body->chunkShapes[0])) {
            b3DestroyShape(body->chunkShapes[0], false);
        }
        if (body->heightField != nullptr) {
            b3DestroyHeightField(body->heightField);
            body->heightField = nullptr;
        }
        createHeightFieldShape(body->body, entity, *asset, *body);

        const glm::mat4 transform = entity.calculateWorldSpaceTransform().getLocalTransform();
        float minHeight = TerrainData2D::decodeHeight(data.heights[0]);
        float maxHeight = minHeight;
        for (uint16_t raw : data.heights) {
            const float height = TerrainData2D::decodeHeight(raw);
            minHeight = std::min(minHeight, height);
            maxHeight = std::max(maxHeight, height);
        }
        const glm::vec3 localMin(minX * data.cellSize, minHeight - 1.f, minZ * data.cellSize);
        const glm::vec3 localMax(maxX * data.cellSize, maxHeight + 1.f, maxZ * data.cellSize);
        glm::vec3 worldMin, worldMax;
        localBoxToWorldAABB(transform, localMin, localMax, worldMin, worldMax);
        wakeBodiesInAABB(worldId, worldMin - glm::vec3(0.5f), worldMax + glm::vec3(0.5f));
    }

} // namespace TerrainPhysics

} // namespace Panda
