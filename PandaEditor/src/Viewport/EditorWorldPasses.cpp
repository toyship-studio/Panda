#include "Viewport/EditorWorldPasses.hpp"

#include <Foundation/Profile.hpp>
#include <Panda/Assets/StaticResources.hpp>
#include <PandaEditorKit/Selection.hpp>

namespace Panda::EditorWorldPasses {

void renderSelectionOverlay(WorldView &view, World &world, Miren::ViewId selectionView) {
    Renderer2D &renderer2d = view.renderer2D();
    Renderer3D &renderer3d = view.renderer3D();
    renderer2d.begin(RenderingMode::GEOMETRY_ONLY, selectionView);
    renderer3d.begin(RenderingMode::GEOMETRY_ONLY, selectionView);
    renderer3d.setViewProj(view.camera().viewProj);
    // What is selected is the entity with everything under it: a model is an entity per node,
    // and the one the user picks in the Hierarchy is its root, which has no geometry of its own.
    std::unordered_set<Entity> selected;
    std::vector<Entity> pending;
    for (Entity entity : world.getById(Editor::selection().entityIds())) { pending.push_back(entity); }
    while (!pending.empty()) {
        Entity entity = pending.back();
        pending.pop_back();
        if (!entity.isValid() || !selected.insert(entity).second) { continue; }
        for (UUID child : entity.getChildEntities()) { pending.push_back(world.getById(child)); }
    }
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
    renderer2d.end();
    renderer3d.end();
}

void renderPicking(WorldView &view, World &world, Miren::ViewId idsView) {
    PANDA_PROFILE_SCOPE_N("World: IDS pass");
    view.renderPass(world, RenderingMode::IDS, idsView);
}

} // namespace Panda::EditorWorldPasses
