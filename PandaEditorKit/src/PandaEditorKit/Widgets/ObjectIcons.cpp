#include "PandaEditorKit/Widgets/ObjectIcons.hpp"

#include "PandaEditorKit/EditorExtensions.hpp"
#include "PandaEditorKit/Widgets/Icons.hpp"

#include <Panda/Extensions/Extensions.hpp>
#include <Panda/GameLogic/Components/BaseComponents.hpp>
#include <Panda/GameLogic/GameContext.hpp>

namespace Editor {

// The icon of the component that says what the entity is (ComponentInspector::characteristic),
// else of a component it merely carries, else of its scripts.
const char *entityIconName(Panda::Entity entity) {
    if (!entity.isValid()) { return Ic::EntityCube; }
    const ComponentInspectorRegistry &inspectors = editorExtensions().componentInspectors;
    for (const bool characteristic : {true, false}) {
        for (const ComponentInspector &inspector : inspectors.inspectors()) {
            if (inspector.characteristic != characteristic || inspector.icon.empty()) { continue; }
            if (inspectors.has(inspector, entity)) { return inspector.icon.c_str(); }
        }
    }
    if (entity.hasComponent<Panda::ScriptListComponent>()) { return Ic::Script; }
    return Ic::EntityCube;
}

// The icon of the kind in the editor registry, by the name of the kind in the registry of the
// engine — for the kinds of the core and of the modules alike.
const char *assetIconName(Panda::AssetType type) {
    if (const Panda::AssetKind *kind = Panda::GameContext::getExtensions().assetKinds.find(type)) {
        if (const AssetKindUi *ui = editorExtensions().assetKinds.find(kind->name); ui && !ui->icon.empty()) {
            return ui->icon.c_str();
        }
    }
    return Ic::FileGeneric;
}

} // namespace Editor
