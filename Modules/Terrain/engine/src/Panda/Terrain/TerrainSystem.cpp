#include "Panda/Terrain/TerrainSystem.hpp"

#include <Panda/GameLogic/GameContext.hpp>
#include "Panda/Terrain/TerrainLodSettings.hpp"
#include "Panda/Terrain/TerrainComponents.hpp"
#include "Panda/GameLogic/World.hpp"
#include "Panda/Renderer/Renderer3D.hpp"
#include "Panda/Terrain/Terrain2DAsset.hpp"
#include "Panda/Terrain/Terrain3DAsset.hpp"
#include "Panda/Terrain/FloraDistances.hpp"
#include "Panda/Terrain/TerrainPhysics.hpp"

#include <Foundation/Profile.hpp>

namespace Panda::Terrain {

namespace {

    void submit(World &world, Renderer3D &renderer, const glm::vec3 &cameraPosition, const glm::mat4 &viewProj) {
        PANDA_PROFILE_SCOPE_N("World: terrain");
        TypeContext &project = GameContext::getProjectSettings();
        const FloraDistances &flora = project.get<FloraDistances>();
        const TerrainLodSettings &lodSettings = project.get<TerrainLodSettings>();
        int submits = 0;
        auto view2d = world.getAllEntitiesWith<IdComponent, Terrain2DComponent, TransformComponent>();
        for (auto entityHandle : view2d) {
            if (!world.isValidEntt(entityHandle)) { continue; }
            auto &terrainComponent = view2d.get<Terrain2DComponent>(entityHandle);
            // .get() filters out both an unloaded asset and a reference of a foreign type
            // (dynamic_cast).
            AssetRef<Terrain2DAsset> terrain = terrainComponent.terrain.as<Terrain2DAsset>();
            if (!terrain.get()) { continue; }
            submits += terrain->render(
                renderer, view2d.get<IdComponent>(entityHandle).id,
                world.getWorldSpaceTransformMatrix(world.entity(entityHandle)), lodSettings, flora,
                cameraPosition, viewProj, terrainComponent.material
            );
        }
        auto view3d = world.getAllEntitiesWith<IdComponent, Terrain3DComponent, TransformComponent>();
        for (auto entityHandle : view3d) {
            if (!world.isValidEntt(entityHandle)) { continue; }
            auto &terrainComponent = view3d.get<Terrain3DComponent>(entityHandle);
            AssetRef<Terrain3DAsset> terrain = terrainComponent.terrain.as<Terrain3DAsset>();
            if (!terrain.get()) { continue; }
            submits += terrain->render(
                renderer, view3d.get<IdComponent>(entityHandle).id,
                world.getWorldSpaceTransformMatrix(world.entity(entityHandle)), lodSettings, cameraPosition, viewProj,
                terrainComponent.material
            );
        }
        PANDA_PROFILE_PLOT("World terrain submits", submits);
    }

    // Crowns of the flora trees of all terrains within the world AABB.
    void collectOccluders(World &world, const glm::vec3 &minWorld, const glm::vec3 &maxWorld, std::vector<Occluder> &out) {
        auto view = world.getAllEntitiesWith<Terrain2DComponent, TransformComponent>();
        for (auto entityHandle : view) {
            if (!world.isValidEntt(entityHandle)) { continue; }
            auto &terrainComponent = view.get<Terrain2DComponent>(entityHandle);
            AssetRef<Terrain2DAsset> terrain = terrainComponent.terrain.as<Terrain2DAsset>();
            if (!terrain.get()) { continue; }
            terrain->collectOccluders(
                world.getWorldSpaceTransformMatrix(world.entity(entityHandle)), minWorld, maxWorld, out
            );
        }
    }

    // Version of the crown source (the scattering + the layer crown densities of each terrain).
    // 0 — the world has no terrain with flora: nothing of this system occludes.
    uint64_t occluderVersion(World &world) {
        uint64_t version = 1469598103934665603ull;
        bool any = false;
        auto view = world.getAllEntitiesWith<Terrain2DComponent, TransformComponent>();
        for (auto entityHandle : view) {
            if (!world.isValidEntt(entityHandle)) { continue; }
            auto &terrainComponent = view.get<Terrain2DComponent>(entityHandle);
            AssetRef<Terrain2DAsset> terrain = terrainComponent.terrain.as<Terrain2DAsset>();
            if (!terrain.get()) { continue; }
            version = (version ^ terrain->occluderVersion()) * 1099511628211ull;
            version = (version ^ static_cast<uint64_t>(entityHandle)) * 1099511628211ull;
            any = true;
        }
        return !any ? 0 : version != 0 ? version : 1;
    }

} // namespace

WorldSystem worldSystem() {
    WorldSystem system;
    system.name = "terrain";
    system.submit = submit;
    system.collectOccluders = collectOccluders;
    system.occluderVersion = occluderVersion;
    // The terrain bodies live in the 3D world of the physics system: they are created after it
    // starts and destroyed before it stops (the stop hooks run in the reverse order).
    system.after = {"physics"};
    system.start = TerrainPhysics::start;
    system.stop = TerrainPhysics::stop;
    system.entityRemoved = TerrainPhysics::entityRemoved;
    system.entityRestored = TerrainPhysics::entityRestored;
    system.transformChanged = TerrainPhysics::transformChanged;
    return system;
}

} // namespace Panda::Terrain
