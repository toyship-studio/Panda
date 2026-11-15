#include "UI/EntityActions.hpp"

#include "App/EditorLayer.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "UI/EditorController.hpp"
#include "UI/EditorDialogs.hpp"

#include <Panda/Assets/Base/AssetImporterBase.hpp>
#include <Panda/GameLogic/Components/BaseComponents.hpp>
#include <Panda/GameLogic/Components/TransformComponent.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/GameLogic/World.hpp>
#include <Panda/ScriptEngine/ExternalScript.hpp>
#include <Panda/ScriptEngine/ScriptEngine.hpp>
#include <Panda/Serialization/WorldMapper.hpp>
#include <Panda/WorldCommands/CommandHistory.hpp>
#include <Panda/WorldCommands/Impl/AddRemoveEntitiesCommand.hpp>
#include <Panda/WorldCommands/Impl/EditScriptFieldCommand.hpp>
#include <Panda/WorldCommands/Impl/EntityTransformCommand.hpp>
#include <Panda/WorldCommands/Impl/HierarchyCommand.hpp>
#include <PandaEditorKit/ComponentCommands.hpp>
#include <PandaEditorKit/EditorExtensions.hpp>
#include <PandaEditorKit/Selection.hpp>
#include <PandaUI/Core/Clipboard.hpp>
#include <Rain/Coders/JsonDecoder.hpp>
#include <Rain/Coders/JsonEncoder.hpp>

#include <sstream>
#include <unordered_set>

namespace Editor {

using namespace PandaUI;

namespace {

    // The entities with everything under them (the Static flag goes to the children too, as in Unity).
    std::unordered_set<Panda::Entity> collectSubtree(Panda::World &world, const std::vector<Panda::Entity> &roots) {
        std::unordered_set<Panda::Entity> result;
        std::vector<Panda::Entity> stack(roots.begin(), roots.end());
        while (!stack.empty()) {
            Panda::Entity entity = stack.back();
            stack.pop_back();
            if (!entity.isValid() || !result.insert(entity).second) { continue; }
            for (Panda::UUID childId : entity.getChildEntities()) {
                Panda::Entity child = world.getById(childId);
                if (child.isValid()) { stack.push_back(child); }
            }
        }
        return result;
    }

    // The value a field of a script starts with, by its type (the fields of a new ExternalScript).
    Panda::ScriptFieldValue defaultScriptValue(Panda::ScriptFieldType type) {
        switch (type) {
            case Panda::ScriptFieldType::INTEGER: return int32_t(0);
            case Panda::ScriptFieldType::FLOAT: return 0.f;
            default: return Panda::UUID(0); // ENTITY/TEXTURE/MATERIAL/WORLD/UNKNOWN — a reference by UUID
        }
    }

    std::vector<Panda::Entity> entitiesOf(Panda::World &world, const std::vector<Panda::UUID> &ids) {
        std::vector<Panda::Entity> entities;
        for (Panda::UUID id : ids) {
            Panda::Entity entity = world.getById(id);
            if (entity.isValid()) { entities.push_back(entity); }
        }
        return entities;
    }

} // namespace

EntityActions::EntityActions(EditorController &workspace, Panda::EditorLayer &app)
    : m_workspace(workspace)
    , m_app(app) {}

Panda::World *EntityActions::world() {
    return m_app.panelsWorld();
}

/* ------------------------------ The targets of an edit ------------------------------ */

std::vector<Panda::Entity> EntityActions::targets() {
    Panda::World *shown = world();
    if (shown == nullptr) { return {}; }
    return m_targetActive ? entitiesOf(*shown, m_target) : selection().entities();
}

Panda::Entity EntityActions::primaryTarget() {
    for (Panda::Entity entity : targets()) { return entity; }
    return Panda::Entity();
}

std::vector<Panda::UUID> EntityActions::targetIds() {
    std::vector<Panda::UUID> ids;
    for (Panda::Entity entity : targets()) { ids.push_back(entity.getId()); }
    return ids;
}

void EntityActions::beginTarget(const std::vector<Panda::UUID> &ids) {
    m_target = ids;
    m_targetActive = true;
}

void EntityActions::endTarget() {
    m_targetActive = false;
    m_target.clear();
}

/* ------------------------------ The Hierarchy ------------------------------ */

void EntityActions::hierarchyDidSelectEntities(const std::vector<Panda::UUID> &ids) {
    m_workspace.selectEntities(ids);
}

CreateActions EntityActions::hierarchyCreateActions(Panda::UUID parentId) {
    CreateActions actions;
    if (world() == nullptr) { return actions; } // no world is open: nothing to create in
    actions.push_back({"Empty Entity", [this, parentId] { createEntity(parentId); }});
    const std::pair<const char *, PrimitiveKind> primitives[] = {
        {"Cube", PrimitiveKind::Cube},   {"Sphere", PrimitiveKind::Sphere},     {"Capsule", PrimitiveKind::Capsule},
        {"Plane", PrimitiveKind::Plane}, {"Cylinder", PrimitiveKind::Cylinder},
    };
    for (const auto &[title, kind] : primitives) {
        actions.push_back({title, [this, parentId, kind = kind] { createPrimitive(parentId, kind); }});
    }
    return actions;
}

void EntityActions::recordCreated(const std::vector<Panda::Entity> &created) {
    Panda::World *shown = world();
    if (shown == nullptr || created.empty()) { return; }
    // The entities exist already: the command is there for Undo.
    Panda::AddRemoveEntitiesCommand command(std::unordered_set<Panda::Entity>(created.begin(), created.end()));
    shown->getCommandHistory().record(command);
    shown->setChanged();
    std::vector<Panda::UUID> ids;
    for (Panda::Entity entity : created) { ids.push_back(entity.getId()); }
    m_workspace.refreshFromWorld();
    m_workspace.selectEntities(ids);
}

void EntityActions::createEntity(Panda::UUID parentId) {
    Panda::World *shown = world();
    if (shown == nullptr) { return; }
    Panda::Entity created = shown->instantiateEntity();
    created.setName("GameObject " + std::to_string(m_nextEntity++));
    shown->setParent(created, shown->getById(parentId)); // no such parent — a root
    recordCreated({created});
}

void EntityActions::createPrimitive(Panda::UUID parentId, PrimitiveKind kind) {
    Panda::World *shown = world();
    if (shown == nullptr) { return; }
    struct PrimitiveSpec {
        const char *name;
        Panda::PrimitiveMeshKind meshKind;
    };
    const PrimitiveSpec spec = [kind]() -> PrimitiveSpec {
        switch (kind) {
            case PrimitiveKind::Sphere: return {"Sphere", Panda::PrimitiveMeshKind::Sphere};
            case PrimitiveKind::Capsule: return {"Capsule", Panda::PrimitiveMeshKind::Capsule};
            case PrimitiveKind::Plane: return {"Plane", Panda::PrimitiveMeshKind::Plane};
            case PrimitiveKind::Cylinder: return {"Cylinder", Panda::PrimitiveMeshKind::Cylinder};
            case PrimitiveKind::Cube:
            default: return {"Cube", Panda::PrimitiveMeshKind::Cube};
        }
    }();

    Panda::Entity created = shown->instantiateEntity();
    created.setName(std::string(spec.name) + " " + std::to_string(m_nextEntity++));
    shown->setParent(created, shown->getById(parentId)); // no such parent — a root
    // An empty material is the built-in checker. A collider is added by hand: the core knows no
    // physics (the components of the Physics module come through Add Component).
    created.addComponent<Panda::PrimitiveMeshComponent>().kind = spec.meshKind;
    recordCreated({created});
}

void EntityActions::hierarchyDidRequestDuplicate(const std::vector<Panda::UUID> &ids) {
    Panda::World *shown = world();
    if (shown == nullptr) { return; }
    std::vector<Panda::Entity> duplicates;
    for (Panda::Entity source : entitiesOf(*shown, ids)) { duplicates.push_back(shown->duplicateEntity(source)); }
    recordCreated(duplicates);
}

void EntityActions::hierarchyDidRequestRename(Panda::UUID id) {
    Panda::World *shown = world();
    Context *context = m_workspace.context();
    Panda::Entity entity = shown ? shown->getById(id) : Panda::Entity();
    if (context == nullptr || !entity.isValid()) { return; }
    // Next to the Hierarchy, where the name is read.
    const Rect frame = m_workspace.hierarchyFrame();
    showNameDialog(
        context, Point(frame.origin.x + 24.f, frame.origin.y + 48.f), "Rename", entity.getName(),
        [this, id](const std::string &name) { rename({id}, name); }, "Rename"
    );
}

void EntityActions::hierarchyDidActivateEntity(Panda::UUID) {
    // The click that came first has made the entity the selection.
    m_app.focusSelection();
}

void EntityActions::rename(const std::vector<Panda::UUID> &ids, const std::string &name) {
    Panda::World *shown = world();
    if (shown == nullptr || name.empty()) { return; }
    std::unordered_set<Panda::Entity> renamed;
    for (Panda::Entity entity : entitiesOf(*shown, ids)) {
        if (entity.getName() != name) { renamed.insert(entity); }
    }
    if (renamed.empty()) { return; }
    // The name is the tag component: one edit command, so Undo brings the old names back.
    commitComponent<Panda::TagComponent>(*shown, renamed, Panda::TagComponent(name));
    shown->setChanged();
    m_workspace.refreshFromWorld();
}

void EntityActions::hierarchyDidRequestCopy(const std::vector<Panda::UUID> &ids) {
    Panda::World *shown = world();
    if (shown == nullptr) { return; }
    const std::vector<Panda::Entity> entities = entitiesOf(*shown, ids);
    if (entities.empty()) { return; }
    // The clipboard holds text: a fragment of a world file. It pastes into this world, into another
    // world or project (another editor window), and reads in any text editor.
    Rain::JsonEncoder encoder(true);
    std::ostringstream text;
    Panda::WorldMapper::saveEntities(*shown, entities, encoder, text);
    setClipboardText(text.str());
}

void EntityActions::hierarchyDidRequestPaste() {
    Panda::World *shown = world();
    if (shown == nullptr) { return; }
    const std::string text = getClipboardText();
    if (text.empty()) { return; }
    Rain::JsonDecoder decoder;
    std::istringstream stream(text);
    std::vector<Panda::Entity> added;
    if (!Panda::WorldMapper::loadEntities(*shown, decoder, stream, added) || added.empty()) {
        LOG_WARN_EDITOR("Paste — the clipboard holds no entities (a fragment of a world file)");
        return;
    }
    recordCreated(added);
}

void EntityActions::hierarchyDidRequestDelete(const std::vector<Panda::UUID> &ids) {
    Panda::World *shown = world();
    if (shown == nullptr || ids.empty()) { return; }
    Panda::AddRemoveEntitiesCommand command(shown->getById(std::unordered_set<Panda::UUID>(ids.begin(), ids.end())));
    shown->getCommandHistory().execute(command); // the entities go with everything under them
    shown->setChanged();
    selection().deselectEntities();
    m_workspace.refreshFromWorld();
}

void EntityActions::hierarchyDidReparent(const std::vector<Panda::UUID> &items, Panda::UUID newParentId) {
    Panda::World *shown = world();
    if (shown == nullptr) { return; }
    const std::vector<Panda::Entity> entities = entitiesOf(*shown, items);
    if (entities.empty()) { return; }
    // A drop onto the root of the world makes roots: the command takes "no parent" too.
    Panda::Entity parent = newParentId != Panda::UUID(0) ? shown->getById(newParentId) : Panda::Entity();
    Panda::HierarchyCommand command(entities, parent);
    shown->getCommandHistory().execute(command);
    shown->setChanged();
    m_workspace.refreshFromWorld();
}

void EntityActions::instantiateMeshes(const std::vector<Panda::AssetId> &meshes) {
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    Panda::World *shown = world();
    if (handler == nullptr || shown == nullptr || meshes.empty()) { return; }
    // The engine builds the nodes, the transforms and the submeshes with their materials: one root
    // per asset.
    std::vector<Panda::Entity> roots;
    const auto &registry = handler->getRegistry();
    for (Panda::AssetId id : meshes) {
        auto it = registry.find(id);
        if (it == registry.end() || it->second.type != Panda::AssetType::IMPORTED_MESH) { continue; }
        const Panda::UUID rootId = Panda::AssetImporterBase::addGltfSceneToWorld(
            handler->getProjectPath() / handler->assetPath(id), Panda::AssetRef<Panda::ImportedMeshAsset>(id), *shown
        );
        Panda::Entity root = shown->getById(rootId);
        if (root.isValid()) { roots.push_back(root); }
    }
    if (roots.empty()) { return; }
    recordCreated(roots);
    LOG_INFO_EDITOR("Added %zu %s to the world", roots.size(), roots.size() == 1 ? "mesh" : "meshes");
}

/* ------------------------------ The entity tabs ------------------------------ */

void EntityActions::entityDidEditName(const std::string &name) {
    rename(targetIds(), name);
}

void EntityActions::entityDidSetStatic(bool isStatic) {
    Panda::World *shown = world();
    if (shown == nullptr) { return; }
    const std::vector<Panda::Entity> edited = targets();
    if (edited.empty()) { return; }
    const std::unordered_set<Panda::Entity> own(edited.begin(), edited.end());
    const std::unordered_set<Panda::Entity> subtree = collectSubtree(*shown, edited);
    const auto apply = [shown, isStatic](const std::unordered_set<Panda::Entity> &entities) {
        toggleComponentPresence<Panda::IsStaticComponent>(*shown, entities, isStatic);
    };
    const auto refresh = [this] { m_workspace.selectionChanged(); }; // the header rereads the flag (also after Cancel)
    Context *context = m_workspace.context();
    if (subtree.size() == own.size() || context == nullptr) {
        apply(own);
        refresh();
        return;
    }
    showApplyToChildrenDialog(
        context, isStatic ? "Enable Static for children too?" : "Disable Static for children too?",
        "The selection has child objects. Apply the change to them as well?",
        [apply, own, subtree](bool includeChildren) { apply(includeChildren ? subtree : own); }, refresh
    );
}

void EntityActions::commitTransformEdit(const std::function<void(Panda::Entity)> &apply, bool scaleEdit) {
    Panda::World *shown = world();
    if (shown == nullptr) { return; }
    std::unordered_set<Panda::Entity> edited;
    for (Panda::Entity entity : targets()) { edited.insert(entity); }
    if (edited.empty()) { return; }
    Panda::EntityTransformCommand command(edited);
    command.saveBeforeEdit();
    for (Panda::Entity entity : edited) {
        apply(entity);
        if (scaleEdit) { // the systems rebuild what depends on the scale (collider shapes)
            entity.scaleUpdated();
        } else {
            entity.transformUpdated(); // a teleport of the runtime state (bodies; the joints stay alive)
        }
    }
    command.saveAfterEdit();
    shown->getCommandHistory().record(command);
    shown->setChanged();
    selection().transformChanged(); // the gizmo follows the edit of the tab
    m_workspace.transformShown();   // the fields show it already: no refresh under the caret
}

// A transform edit of several entities: ONLY the changed axes are applied (a diff against the
// primary one — a mixed axis shows its value), so the axes that differ between the entities stay.
void EntityActions::entityDidEditPosition(glm::vec3 position) {
    Panda::Entity primary = primaryTarget();
    if (!primary) { return; }
    const glm::vec3 old = primary.getTransform().getPosition();
    commitTransformEdit([&](Panda::Entity entity) {
        glm::vec3 value = entity.getTransform().getPosition();
        if (position.x != old.x) { value.x = position.x; }
        if (position.y != old.y) { value.y = position.y; }
        if (position.z != old.z) { value.z = position.z; }
        entity.getTransform().setPosition(value);
    });
}

void EntityActions::entityDidEditRotation(glm::vec3 rotation) {
    Panda::Entity primary = primaryTarget();
    if (!primary) { return; }
    const glm::vec3 old = primary.getTransform().getRotationEuler();
    commitTransformEdit([&](Panda::Entity entity) {
        glm::vec3 value = entity.getTransform().getRotationEuler();
        if (rotation.x != old.x) { value.x = rotation.x; }
        if (rotation.y != old.y) { value.y = rotation.y; }
        if (rotation.z != old.z) { value.z = rotation.z; }
        entity.getTransform().setRotationEuler(value);
    });
}

void EntityActions::entityDidEditScale(glm::vec3 scale) {
    Panda::Entity primary = primaryTarget();
    if (!primary) { return; }
    const glm::vec3 old = primary.getTransform().getScale();
    commitTransformEdit(
        [&](Panda::Entity entity) {
            glm::vec3 value = entity.getTransform().getScale();
            if (scale.x != old.x) { value.x = scale.x; }
            if (scale.y != old.y) { value.y = scale.y; }
            if (scale.z != old.z) { value.z = scale.z; }
            entity.getTransform().setScale(value);
        },
        /*scaleEdit*/ true
    );
}

void EntityActions::entityDidAddComponent(const std::string &component) {
    setComponentPresence(component, /*add*/ true);
}

void EntityActions::entityDidRemoveComponent(const std::string &component) {
    setComponentPresence(component, /*add*/ false);
}

void EntityActions::setComponentPresence(const std::string &component, bool add) {
    Panda::World *shown = world();
    const ComponentInspector *inspector = editorExtensions().componentInspectors.find(component);
    if (shown == nullptr || inspector == nullptr || inspector->setPresence == nullptr) { return; }
    inspector->setPresence(*shown, targets(), add);
    // The tab rereads the entity — the section appears or goes — and the Hierarchy its icon: a
    // component may be what the entity is there (a camera, a light).
    m_workspace.refreshFromWorld();
}

void EntityActions::entityDidAddScript(const std::string &className) {
    Panda::ScriptEngine *engine = Panda::GameContext::getScriptEngine();
    if (world() == nullptr || engine == nullptr || !engine->isLoaded()) { return; }
    const Panda::ScriptClassManifest clazz = engine->getManifest().getClass(className.c_str());
    if (!clazz) { return; }
    for (Panda::Entity entity : targets()) { // the script goes to every target
        if (!entity.hasComponent<Panda::ScriptListComponent>()) { entity.addComponent<Panda::ScriptListComponent>(); }
        std::vector<Panda::ScriptField> fields;
        for (const Panda::ScriptFieldManifest &field : clazz.fields) {
            fields.emplace_back(0u, field.handle, field.name, field.type, defaultScriptValue(field.type));
        }
        entity.addScript(
            Panda::ExternalScript(static_cast<Panda::Handle>(entity.getId()), 0u, clazz.name, fields, clazz.order)
        );
    }
    m_workspace.selectionChanged(); // a Script section with the real fields appears
}

void EntityActions::entityDidRemoveScript(int scriptIndex) {
    Panda::Entity primary = primaryTarget();
    if (primary && primary.hasComponent<Panda::ScriptListComponent>()) {
        std::vector<Panda::ExternalScript> &scripts = primary.getComponent<Panda::ScriptListComponent>().scripts;
        if (scriptIndex >= 0 && scriptIndex < static_cast<int>(scripts.size())) {
            scripts.erase(scripts.begin() + scriptIndex); // script ids are 0 outside the runtime: by index
            primary.setWorldChanged();
        }
    }
    m_workspace.selectionChanged();
}

void EntityActions::entityDidEditScriptField(
    int scriptIndex, const std::string &fieldName, Panda::ScriptFieldValue value
) {
    Panda::World *shown = world();
    Panda::Entity primary = primaryTarget();
    if (shown == nullptr || !primary || !primary.hasComponent<Panda::ScriptListComponent>()) { return; }
    Panda::ScriptListComponent &list = primary.getComponent<Panda::ScriptListComponent>();
    if (scriptIndex < 0 || scriptIndex >= static_cast<int>(list.scripts.size())) { return; }
    Panda::ExternalScript &script = list.scripts[scriptIndex];
    if (!script.hasField(fieldName.c_str())) { return; }
    // The command writes the value: into the world and, in Play, into the running script — the
    // same on undo and redo.
    Panda::EditScriptFieldCommand command(
        primary, static_cast<size_t>(scriptIndex), fieldName, script.getField(fieldName.c_str()).value, std::move(value)
    );
    shown->getCommandHistory().execute(command);
}

void EntityActions::entityDidClickBackground() {
    m_workspace.deselectAll();
}

} // namespace Editor
