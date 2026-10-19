#include "UI/Widgets/ObjectIcons.hpp"

#include <PandaEditorKit/EditorExtensions.hpp>
#include <PandaEditorKit/Widgets/Icons.hpp>

#include <Panda/Extensions/Extensions.hpp>
#include <Panda/GameLogic/Components/BaseComponents.hpp>
#include <Panda/GameLogic/GameContext.hpp>

namespace Editor {

const char *entityIconName(Panda::Entity entity) {
    if (!entity.isValid()) { return Ic::EntityCube; }
    if (entity.hasComponent<Panda::CameraComponent>()) { return Ic::Camera; }
    if (entity.hasComponent<Panda::DirectionalLightComponent>()) { return Ic::LightSun; }
    if (entity.hasComponent<Panda::PointLightComponent>()) { return Ic::LightPoint; }
    // A module component is the most characteristic thing in an entity (terrain etc.).
    const ComponentInspectorRegistry &inspectors = editorExtensions().componentInspectors;
    for (const ComponentInspector &inspector : inspectors.inspectors()) {
        if (!inspector.icon.empty() && inspectors.has(inspector, entity)) { return inspector.icon.c_str(); }
    }
    if (entity.hasComponent<Panda::SkyComponent>()) { return Ic::Globe; }
    if (entity.hasComponent<Panda::SpriteRendererComponent>()) { return Ic::Sprite; }
    if (entity.hasComponent<Panda::RuntimeMeshComponent>() ||
        entity.hasComponent<Panda::PrimitiveMeshComponent>() ||
        entity.hasComponent<Panda::ImportedSubmeshComponent>()) {
        return Ic::Mesh;
    }
    if (entity.hasComponent<Panda::ScriptListComponent>()) { return Ic::Script; }
    return Ic::EntityCube;
}

const char *assetIconName(Panda::AssetType type) {
    if (type == Panda::AssetType::TEXTURE) { return Ic::FilePng; }
    if (type == Panda::AssetType::CUBE_MAP) { return Ic::Globe; }
    if (type == Panda::AssetType::SHADER) { return Ic::FileCpp; }
    if (type == Panda::AssetType::MATERIAL) { return Ic::Material; }
    if (type == Panda::AssetType::RUNTIME_MESH || type == Panda::AssetType::IMPORTED_MESH) { return Ic::Mesh; }
    if (type == Panda::AssetType::WORLD) { return Ic::AssetWorld; }
    if (type == Panda::AssetType::POSTPROCESSING) { return Ic::Sprite; }
    // Module kind: the icon comes from the editor registry by the kind name.
    if (const Panda::AssetKind *kind = Panda::GameContext::getExtensions().assetKinds.find(type)) {
        if (const AssetKindUi *ui = editorExtensions().assetKinds.find(kind->name); ui && !ui->icon.empty()) {
            return ui->icon.c_str();
        }
    }
    return Ic::FileGeneric;
}

} // namespace Editor
