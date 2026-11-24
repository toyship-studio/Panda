#include "UI/EntityActions.hpp"
#include "UI/WorldTabs.hpp"

#include "App/EditorLayer.hpp"
#include "Project/Documents/WorldFile.hpp"
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
#include <Panda/WorldCommands/Impl/CompositeCommand.hpp>
#include <Panda/WorldCommands/Impl/EditScriptFieldCommand.hpp>
#include <Panda/WorldCommands/Impl/EntityTransformCommand.hpp>
#include <Panda/WorldCommands/Impl/HierarchyCommand.hpp>
#include <Panda/WorldCommands/Impl/InstanceEntryCommand.hpp>
#include <Panda/Prefabs/PrefabAsset.hpp>
#include <PandaEditorKit/ComponentCommands.hpp>
#include <PandaEditorKit/EditorExtensions.hpp>
#include <PandaEditorKit/Selection.hpp>
#include <Fern/Files/AtomicWrite.hpp>
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

Panda::Entity EntityActions::prefabRoot() {
    // The game plays a world, never a prefab.
    Panda::WorldFile *active = m_app.isPlaying() ? nullptr : m_app.documents().activeWorld();
    return active != nullptr ? active->prefabRoot() : Panda::Entity();
}

Panda::AssetId EntityActions::shownPrefab() {
    Panda::WorldFile *active = m_app.isPlaying() ? nullptr : m_app.documents().activeWorld();
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    return active != nullptr && active->isPrefab() && handler != nullptr ? handler->getAssetId(active->path()) : Panda::AssetId(0);
}

Panda::Entity EntityActions::parentFor(Panda::UUID parentId) {
    Panda::World *shown = world();
    if (shown == nullptr) { return {}; }
    Panda::Entity parent = shown->getById(parentId);
    return parent.isValid() ? parent : prefabRoot();
}

/* ------------------------------ The targets of an edit ------------------------------ */

std::vector<Panda::Entity> EntityActions::targets() {
    if (world() == nullptr) { return {}; }
    return selection().entities();
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
    shown->setParent(created, parentFor(parentId)); // no such parent — a root
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
    shown->setParent(created, parentFor(parentId)); // no such parent — a root
    // An empty material is the built-in checker. A collider is added by hand: the core knows no
    // physics (the components of the Physics module come through Add Component).
    created.addComponent<Panda::PrimitiveMeshComponent>().kind = spec.meshKind;
    recordCreated({created});
}

void EntityActions::hierarchyDidRequestDuplicate(const std::vector<Panda::UUID> &ids) {
    Panda::World *shown = world();
    if (shown == nullptr) { return; }
    std::vector<Panda::Entity> duplicates;
    const Panda::Entity root = prefabRoot();
    for (Panda::Entity source : entitiesOf(*shown, ids)) {
        if (source == root) {
            LOG_WARN_EDITOR("A prefab has one root: %s is not duplicated.", source.getName().c_str());
            continue;
        }
        duplicates.push_back(shown->duplicateEntity(source));
    }
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
    // world or project (another editor window), and reads in any text editor. An instance of a
    // prefab is there as a file holds it: the prefab and what differs.
    Rain::JsonEncoder encoder(true);
    std::ostringstream text;
    Panda::WorldMapper::saveEntities(*shown, entities, encoder, text);
    setClipboardText(Panda::Prefabs::collapse(text.str()));
}

void EntityActions::hierarchyDidRequestPaste() {
    Panda::World *shown = world();
    if (shown == nullptr) { return; }
    const std::string text = getClipboardText();
    if (text.empty()) { return; }
    Rain::JsonDecoder decoder;
    std::string expanded;
    std::vector<Panda::Entity> added;
    if (Panda::Prefabs::expand(text, expanded)) {
        std::istringstream stream(expanded);
        Panda::WorldMapper::loadEntities(*shown, decoder, stream, added);
    }
    if (added.empty()) {
        LOG_WARN_EDITOR("Paste — the clipboard holds no entities (a fragment of a world file)");
        return;
    }
    // In a prefab everything stands under its root.
    if (Panda::Entity root = prefabRoot(); root.isValid()) {
        for (Panda::Entity entity : added) { shown->setParent(entity, root); }
    }
    recordCreated(added);
}

void EntityActions::hierarchyDidRequestDelete(const std::vector<Panda::UUID> &ids) {
    Panda::World *shown = world();
    if (shown == nullptr || ids.empty()) { return; }
    std::unordered_set<Panda::Entity> deleted = shown->getById(std::unordered_set<Panda::UUID>(ids.begin(), ids.end()));
    if (Panda::Entity root = prefabRoot(); root.isValid() && deleted.erase(root) > 0) {
        LOG_WARN_EDITOR("%s is the root of the prefab and stays: a prefab is the tree under it.", root.getName().c_str());
    }
    if (deleted.empty()) { return; }
    Panda::AddRemoveEntitiesCommand command(deleted);
    shown->getCommandHistory().execute(command); // the entities go with everything under them
    shown->setChanged();
    selection().deselectEntities();
    m_workspace.refreshFromWorld();
}

void EntityActions::hierarchyDidReparent(const std::vector<Panda::UUID> &items, Panda::UUID newParentId) {
    Panda::World *shown = world();
    if (shown == nullptr) { return; }
    std::vector<Panda::Entity> entities = entitiesOf(*shown, items);
    // The root of a prefab stays its root, and nothing else of the prefab stands beside it.
    if (Panda::Entity root = prefabRoot(); root.isValid()) { std::erase(entities, root); }
    if (entities.empty()) { return; }
    // A drop onto the root of the world makes roots: the command takes "no parent" too.
    Panda::Entity parent = parentFor(newParentId);
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
    if (Panda::Entity prefab = prefabRoot(); prefab.isValid()) {
        for (Panda::Entity root : roots) { shown->setParent(root, prefab); }
    }
    recordCreated(roots);
    LOG_INFO_EDITOR("Added %zu %s to the world", roots.size(), roots.size() == 1 ? "mesh" : "meshes");
}

/* ------------------------------ Prefabs ------------------------------ */

void EntityActions::instantiatePrefabs(const std::vector<Panda::AssetId> &prefabs, Panda::UUID parentId) {
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    Panda::World *shown = world();
    if (handler == nullptr || shown == nullptr || prefabs.empty()) { return; }
    const Panda::AssetId inside = shownPrefab();
    Panda::EntityPlacement placement;
    placement.parent = parentFor(parentId);
    std::vector<Panda::Entity> roots;
    for (Panda::AssetId prefab : prefabs) {
        if (handler->getInfo(prefab).type != Panda::AssetType::PREFAB) { continue; }
        // An instance of a prefab inside itself — or inside a prefab it holds — would hold itself.
        if (inside != Panda::AssetId(0) && Panda::Prefabs::dependsOn(prefab, inside)) {
            LOG_WARN_EDITOR(
                "%s holds %s: an instance of it here would hold itself.", handler->getAssetName(prefab).c_str(),
                handler->getAssetName(inside).c_str()
            );
            continue;
        }
        Panda::Entity root = shown->instantiate(Panda::AssetRef<Panda::PrefabAsset>(prefab), Panda::UUID(0), placement);
        if (!root.isValid()) {
            LOG_ERROR_EDITOR("%s holds no entity: there is nothing to put into the world.", handler->getAssetName(prefab).c_str());
            continue;
        }
        roots.push_back(root);
    }
    if (roots.empty()) { return; }
    // The scripts of the new entities follow the loaded classes like the rest of the world.
    Panda::ScriptEngine *scripts = Panda::GameContext::getScriptEngine();
    if (scripts != nullptr && scripts->isLoaded() && !shown->isRunning()) { shown->updateScriptsAndFields(); }
    recordCreated(roots);
}

void EntityActions::createPrefab(Panda::UUID id) {
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    Panda::World *shown = world();
    Panda::Entity entity = shown != nullptr ? shown->getById(id) : Panda::Entity();
    if (handler == nullptr || !entity.isValid() || m_app.isPlaying()) { return; }
    if (entity == prefabRoot()) {
        LOG_WARN_EDITOR("%s is the root of this prefab already.", entity.getName().c_str());
        return;
    }
    // A part of an instance that is not its root belongs to that prefab: it is made a prefab there.
    if (Panda::Entity root = Panda::Prefabs::instanceRoot(entity); root.isValid() && !(root == entity)) {
        LOG_WARN_EDITOR(
            "%s is a part of an instance of %s: open that prefab to make a prefab of a part of it, or unpack the instance.",
            entity.getName().c_str(), handler->getAssetName(Panda::Prefabs::prefabOf(entity)).c_str()
        );
        return;
    }
    // The file: in the folder the Content Browser shows, named after the entity.
    const Panda::path_t directory = m_workspace.contentDirectory();
    if (directory.empty()) { return; }
    std::string name = entity.getName();
    for (char &c : name) {
        if (c == '/' || c == '\\' || c == ':') { c = ' '; }
    }
    const Panda::path_t path = uniqueAssetPath(directory, name.empty() ? "Prefab" : name, ".prefab");
    std::string error;
    if (!Fern::writeFileAtomically(path, Panda::Prefabs::createFrom(entity), &error)) {
        LOG_ERROR_EDITOR("The prefab is not made: %s.", error.c_str());
        return;
    }
    const Panda::AssetId prefab = handler->documents().registerCreatedAsset(path);
    if (!prefab) { return; }
    // The entities stand in the world as an instance of the new prefab from now on. Their ids
    // change with it, so the steps of the history that name them are over.
    Panda::Prefabs::convertToInstance(entity, prefab);
    shown->getCommandHistory().clear();
    m_workspace.refreshAssetPanels();
    m_workspace.refreshFromWorld();
    m_workspace.selectEntities({entity.getId()});
}

void EntityActions::unpackPrefabs(const std::vector<Panda::UUID> &ids) {
    Panda::World *shown = world();
    if (shown == nullptr || m_app.isPlaying()) { return; }
    std::unordered_set<Panda::Entity> roots;
    for (Panda::Entity entity : entitiesOf(*shown, ids)) {
        if (Panda::Entity root = Panda::Prefabs::instanceRoot(entity); root.isValid()) { roots.insert(root); }
    }
    if (roots.empty()) { return; }
    for (Panda::Entity root : roots) { Panda::Prefabs::unpack(root); }
    shown->getCommandHistory().clear(); // the ids of nested instances changed with it
    m_workspace.refreshFromWorld();
}

void EntityActions::openPrefabOf(Panda::UUID id) {
    Panda::World *shown = world();
    Panda::Entity entity = shown != nullptr ? shown->getById(id) : Panda::Entity();
    if (const Panda::AssetId prefab = Panda::Prefabs::prefabOf(entity)) { m_workspace.worldTabs().open(prefab); }
}

bool EntityActions::refusePrefabEditInPlay() {
    if (!m_app.isPlaying()) { return false; }
    // An instance is brought to another entry as a whole — entities come and go, components are
    // read again: not something to do to a world while its scripts and bodies run.
    LOG_WARN_EDITOR("The game is playing: an instance is reverted and applied in Edit. Stop the game first.");
    return true;
}

void EntityActions::revertPrefab(Panda::UUID rootId, const Panda::Prefabs::Scope &scope) {
    if (refusePrefabEditInPlay()) { return; }
    Panda::World *shown = world();
    Panda::Entity root = shown != nullptr ? shown->getById(rootId) : Panda::Entity();
    const std::string before = Panda::Prefabs::entryOf(root);
    const std::string after = Panda::Prefabs::entryWithout(root, scope);
    if (before.empty() || after.empty()) { return; }
    // The whole instance: what was added to it goes too — one step with the entities it brings
    // back to the prefab.
    std::unordered_set<Panda::Entity> additions;
    if (!scope.entity.isValid()) {
        for (const Panda::Prefabs::AddedEntity &added : Panda::Prefabs::differenceOf(root).added) { additions.insert(shown->getById(added.id)); }
    }
    if (before == after && additions.empty()) { return; }
    Panda::CompositeCommand command;
    if (!additions.empty()) { command.add(Panda::AddRemoveEntitiesCommand(additions)); }
    if (before != after) { command.add(Panda::InstanceEntryCommand(*shown, rootId, before, after)); }
    shown->getCommandHistory().execute(command);
    shown->setChanged();
    m_workspace.refreshFromWorld();
}

void EntityActions::restoreRemoved(Panda::UUID rootId, Panda::UUID source) {
    if (refusePrefabEditInPlay()) { return; }
    Panda::World *shown = world();
    Panda::Entity root = shown != nullptr ? shown->getById(rootId) : Panda::Entity();
    const std::string before = Panda::Prefabs::entryOf(root);
    const std::string after = Panda::Prefabs::entryRestoring(root, source);
    if (before.empty() || after.empty() || before == after) { return; }
    Panda::InstanceEntryCommand command(*shown, rootId, before, after);
    shown->getCommandHistory().execute(command);
    m_workspace.refreshFromWorld();
}

void EntityActions::applyPrefab(Panda::UUID rootId, const Panda::Prefabs::Scope &scope, bool forced) {
    if (refusePrefabEditInPlay()) { return; }
    Panda::World *shown = world();
    Panda::Entity root = shown != nullptr ? shown->getById(rootId) : Panda::Entity();
    if (!m_app.documents().applyToPrefab(root, scope, forced)) { return; }
    m_workspace.refreshAssetPanels();
    m_workspace.refreshFromWorld();
}

void EntityActions::revertAddition(Panda::UUID top) {
    if (refusePrefabEditInPlay()) { return; }
    // Out of the instance: deleted, as Delete does it — an undo step.
    hierarchyDidRequestDelete({top});
}

void EntityActions::applyAddition(Panda::UUID top) {
    Panda::World *shown = world();
    Panda::Entity entity = shown != nullptr ? shown->getById(top) : Panda::Entity();
    const Panda::Prefabs::Addition addition = Panda::Prefabs::additionOf(entity);
    if (!addition.instance.isValid()) { return; }
    applyPrefab(addition.instance.getId(), Panda::Prefabs::Scope{addition.top, {}, {}}, /*forced*/ false);
}

/* ------------------------------ The inspector ------------------------------ */

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
    selection().transformChanged(); // the gizmo follows the edit of the inspector
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
    // The inspector rereads the entity — the section appears or goes — and the Hierarchy its icon: a
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
            fields.emplace_back(
                0u, field.handle, field.name, field.type, Panda::defaultScriptFieldValue(field.type, field.list)
            );
        }
        entity.addScript(
            Panda::ExternalScript(static_cast<Panda::Handle>(entity.getId()), 0u, clazz.name, fields, clazz.order)
        );
    }
    m_workspace.refreshInspector(); // a Script section with the real fields appears
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
    m_workspace.refreshInspector();
}

void EntityActions::entityDidEditScriptField(
    int scriptIndex, const std::string &fieldName, Panda::ScriptFieldValue value, bool structural
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
        primary, static_cast<size_t>(scriptIndex), fieldName, script.getField(fieldName.c_str()).value,
        std::move(value), structural
    );
    shown->getCommandHistory().execute(command);
    if (structural) { m_workspace.refreshInspector(); } // the rows of the list are other rows now
}

void EntityActions::entityDidClickBackground() {
    m_workspace.selectEntities({});
}

void EntityActions::entityDidRevertPrefab(bool whole, const std::string &component, const std::string &field) {
    Panda::Entity primary = primaryTarget();
    Panda::Entity root = Panda::Prefabs::instanceRoot(primary);
    if (!root.isValid()) { return; }
    revertPrefab(root.getId(), whole ? Panda::Prefabs::Scope{} : Panda::Prefabs::Scope{primary, component, field});
}

void EntityActions::entityDidApplyPrefab(bool whole, const std::string &component, const std::string &field, bool forced) {
    Panda::Entity primary = primaryTarget();
    Panda::Entity root = Panda::Prefabs::instanceRoot(primary);
    if (!root.isValid()) { return; }
    applyPrefab(root.getId(), whole ? Panda::Prefabs::Scope{} : Panda::Prefabs::Scope{primary, component, field}, forced);
}

void EntityActions::entityDidRestoreRemoved(Panda::UUID source) {
    Panda::Entity root = Panda::Prefabs::instanceRoot(primaryTarget());
    if (root.isValid()) { restoreRemoved(root.getId(), source); }
}

void EntityActions::entityDidOpenPrefab() {
    Panda::Entity primary = primaryTarget();
    if (primary.isValid()) { openPrefabOf(primary.getId()); }
}

void EntityActions::entityDidSelectPrefabRoot() {
    Panda::Entity root = Panda::Prefabs::instanceRoot(primaryTarget());
    if (root.isValid()) { m_workspace.selectEntities({root.getId()}); }
}

void EntityActions::entityDidSelectEntity(Panda::UUID id) {
    Panda::World *shown = world();
    if (shown != nullptr && shown->getById(id).isValid()) { m_workspace.selectEntities({id}); }
}

void EntityActions::entityDidRevertAddition(Panda::UUID top) {
    revertAddition(top);
}

void EntityActions::entityDidApplyAddition(Panda::UUID top) {
    applyAddition(top);
}

} // namespace Editor
