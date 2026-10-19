#include "Viewport/EditorWorldPasses.hpp"

#include <Foundation/Profile.hpp>
#include <Panda/Assets/StaticResources.hpp>
#include <Panda/GameLogic/SelectionContext.hpp>
#include <Panda/Renderer/MirenViewDistribution.hpp>

namespace Panda::EditorWorldPasses {

void renderSelectionOverlay(World &world, const glm::mat4 &viewProj) {
    Renderer2D &renderer2d = world.getRenderer2D();
    Renderer3D &renderer3d = world.getRenderer3D();
    renderer2d.begin(RenderingMode::GEOMETRY_ONLY, Views::SELECTED_GEOMETRY_VIEW);
    renderer3d.begin(RenderingMode::GEOMETRY_ONLY, Views::SELECTED_GEOMETRY_VIEW);
    renderer3d.setViewProj(viewProj);
    const std::unordered_set<Entity> selected = world.getById(SelectionContext::getSelectedEntities());
    for (Entity entity : selected) {
        glm::mat4 transform = world.getWorldSpaceTransformMatrix(entity);
        if (entity.hasComponent<SpriteRendererComponent>()) {
            Renderer2D::RectData rect;
            rect.transform = transform;
            rect.size = {1.f, 1.f};
            renderer2d.drawRect(rect);
        }
        if (entity.hasComponent<PrimitiveMeshComponent>()) {
            AssetRef<RuntimeMeshAsset> mesh =
                StaticResources::primitiveMeshOf(entity.getComponent<PrimitiveMeshComponent>().kind);
            if (mesh) { renderer3d.submit(0, transform, mesh, StaticResources::selectedGeometryMaterial); }
        }
        if (entity.hasComponent<RuntimeMeshComponent>()) {
            auto &meshComponent = entity.getComponent<RuntimeMeshComponent>();
            if (meshComponent.runtimeMesh) {
                renderer3d.submit(
                    0, transform, meshComponent.runtimeMesh.as<RuntimeMeshAsset>(),
                    StaticResources::selectedGeometryMaterial
                );
            }
        }
        if (entity.hasComponent<ImportedSubmeshComponent>()) {
            auto &meshComponent = entity.getComponent<ImportedSubmeshComponent>();
            if (meshComponent.importedMesh) {
                renderer3d.submit(
                    0, transform, meshComponent.importedMesh.as<ImportedMeshAsset>(), meshComponent.submeshIndex,
                    meshComponent.primitiveIndex, StaticResources::selectedGeometryMaterial
                );
            }
        }
    }
    Miren::touch(Views::SELECTED_GEOMETRY_VIEW); // a view with no draws is not executed — no clear without a selection
    renderer2d.end();
    renderer3d.end();
}

void renderPicking(World &world, const WorldFrame &frame) {
    PANDA_PROFILE_SCOPE_N("World: IDS pass");
    world.renderPass(RenderingMode::IDS, Views::IDS_VIEW, frame);
    Miren::touch(Views::IDS_VIEW); // clears the IDS target even without draw calls
}

} // namespace Panda::EditorWorldPasses
