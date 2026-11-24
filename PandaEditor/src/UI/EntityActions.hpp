#pragma once

#include "UI/CreateAction.hpp"
#include "UI/Documents/EntityEditorView.hpp"
#include "UI/Panels/HierarchyPanel.hpp"

#include <Panda/Assets/Base/Asset.hpp>
#include <Panda/GameLogic/Entity.hpp>
#include <Panda/Prefabs/Prefabs.hpp>

#include <functional>
#include <string>
#include <vector>

namespace Panda {
class EditorLayer;
class World;
} // namespace Panda

namespace Editor {

class EditorController;

// What the user does to the entities of the world the panels show: the intents of the Hierarchy
// (create, duplicate, copy and paste, delete, reparent, rename) and of the inspector of the world
// tab (the name, the Static flag, the transform, components, scripts). Every edit is a command of
// the history of that world, so Undo takes it back; the panels follow through the workspace.
class EntityActions final : public HierarchyPanelOutput, public EntityEditorOutput {
public:
    EntityActions(EditorController &workspace, Panda::EditorLayer &app);

    // The entities an edit applies to: the selection, the entities the inspector shows.
    std::vector<Panda::Entity> targets();
    Panda::Entity primaryTarget(); // the first of them; invalid — none
    std::vector<Panda::UUID> targetIds();

    // The name of entities as one undoable edit (the field of the inspector, Rename of the Hierarchy).
    void rename(const std::vector<Panda::UUID> &ids, const std::string &name);
    // Models become entities of the world: a subtree per mesh asset (a drop onto the viewport,
    // Add to World of the Content Browser).
    void instantiateMeshes(const std::vector<Panda::AssetId> &meshes);

    // ---- Prefabs (context/prefabs.md).
    // An instance of each prefab in the world on screen (in a prefab — under its root). A
    // prefab is not put into itself.
    void instantiatePrefabs(const std::vector<Panda::AssetId> &prefabs, Panda::UUID parentId = Panda::UUID(0));
    // The entity with everything under it becomes a prefab — a file in the folder the Content
    // Browser shows, named after the entity — and stands in the world as an instance of it.
    void createPrefab(Panda::UUID id);
    // The instances the entities are parts of become plain entities.
    void unpackPrefabs(const std::vector<Panda::UUID> &ids);
    // The prefab the entity is a part of, in its tab.
    void openPrefabOf(Panda::UUID id);
    // What an instance differs in from its prefab goes: the whole of it, or the part the scope
    // names (an entity, a component of it, a place of the component). An undo step.
    void revertPrefab(Panda::UUID root, const Panda::Prefabs::Scope &scope);
    // An entity of the prefab the instance removed comes back. An undo step.
    void restoreRemoved(Panda::UUID root, Panda::UUID source);
    // …or goes into the prefab, and every instance of it gets it. `forced` — the other instances
    // lose what they have of their own in the same places. The prefab file is written; not an
    // undo step.
    void applyPrefab(Panda::UUID root, const Panda::Prefabs::Scope &scope, bool forced);
    // Revert, Restore and Apply change an instance as a whole: while the game plays they say
    // so and do nothing (true).
    bool refusePrefabEditInPlay();
    // An addition to an instance (Prefabs::Addition::top) goes out of it — deleted, an undo step —
    // or into its prefab.
    void revertAddition(Panda::UUID top);
    void applyAddition(Panda::UUID top);
    // The root of the prefab the panels show — everything in a prefab stands under it. Invalid —
    // the panels show a world.
    Panda::Entity prefabRoot();

    // HierarchyPanelOutput.
    void hierarchyDidSelectEntities(const std::vector<Panda::UUID> &ids) override;
    CreateActions hierarchyCreateActions(Panda::UUID parentId) override;
    void hierarchyDidRequestDuplicate(const std::vector<Panda::UUID> &ids) override;
    void hierarchyDidRequestRename(Panda::UUID id) override;
    void hierarchyDidActivateEntity(Panda::UUID id) override;
    void hierarchyDidRequestCopy(const std::vector<Panda::UUID> &ids) override;
    void hierarchyDidRequestPaste() override;
    void hierarchyDidRequestDelete(const std::vector<Panda::UUID> &ids) override;
    void hierarchyDidReparent(const std::vector<Panda::UUID> &items, Panda::UUID newParentId) override;
    void hierarchyDidRequestCreatePrefab(Panda::UUID id) override {
        createPrefab(id);
    }
    void hierarchyDidRequestUnpack(const std::vector<Panda::UUID> &ids) override {
        unpackPrefabs(ids);
    }
    void hierarchyDidRequestOpenPrefab(Panda::UUID id) override {
        openPrefabOf(id);
    }

    // EntityEditorOutput.
    void entityDidEditName(const std::string &name) override;
    void entityDidSetStatic(bool isStatic) override;
    void entityDidEditPosition(glm::vec3 position) override;
    void entityDidEditRotation(glm::vec3 rotation) override;
    void entityDidEditScale(glm::vec3 scale) override;
    void entityDidAddComponent(const std::string &component) override;
    void entityDidRemoveComponent(const std::string &component) override;
    void entityDidAddScript(const std::string &className) override;
    void entityDidRemoveScript(int scriptIndex) override;
    void entityDidEditScriptField(
        int scriptIndex, const std::string &fieldName, Panda::ScriptFieldValue value, bool structural
    ) override;
    void entityDidClickBackground() override;
    void entityDidRevertPrefab(bool whole, const std::string &component, const std::string &field) override;
    void entityDidApplyPrefab(bool whole, const std::string &component, const std::string &field, bool forced) override;
    void entityDidRestoreRemoved(Panda::UUID source) override;
    void entityDidOpenPrefab() override;
    void entityDidSelectPrefabRoot() override;
    void entityDidSelectEntity(Panda::UUID id) override;
    void entityDidRevertAddition(Panda::UUID top) override;
    void entityDidApplyAddition(Panda::UUID top) override;

private:
    Panda::World *world(); // the world the panels show; nullptr — none is open
    // The entity new things go under: the one that was named, or — nothing named in a prefab —
    // the root of the prefab. Invalid — the root of the world.
    Panda::Entity parentFor(Panda::UUID parentId);
    // The prefab the panels show, as an asset; 0 — they show a world.
    Panda::AssetId shownPrefab();
    void createEntity(Panda::UUID parentId);
    void createPrimitive(Panda::UUID parentId, PrimitiveKind kind);
    // What was just created (a new entity, a copy, a paste, a model) becomes an undo step and the
    // selection.
    void recordCreated(const std::vector<Panda::Entity> &created);
    // A transform edit through a command (undo, merge) and the sync of the systems
    // (Entity::transformUpdated / scaleUpdated): scaleEdit — what depends on the scale is rebuilt,
    // otherwise the runtime state is teleported.
    void commitTransformEdit(const std::function<void(Panda::Entity)> &apply, bool scaleEdit = false);
    // Adds or removes a component by the name of its type on the targets, as one undo step.
    void setComponentPresence(const std::string &component, bool add);

    EditorController &m_workspace;
    Panda::EditorLayer &m_app;
    int m_nextEntity = 1; // counter for the names of new entities
};

} // namespace Editor
