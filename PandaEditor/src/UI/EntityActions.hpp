#pragma once

#include "UI/CreateAction.hpp"
#include "UI/Documents/EntityEditorView.hpp"
#include "UI/Panels/HierarchyPanel.hpp"

#include <Panda/Assets/Base/Asset.hpp>
#include <Panda/GameLogic/Entity.hpp>

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
// (create, duplicate, copy and paste, delete, reparent, rename) and of the entity tabs (the name,
// the Static flag, the transform, components, scripts). Every edit is a command of the history of
// that world, so Undo takes it back; the panels follow through the workspace.
class EntityActions final : public HierarchyPanelOutput, public EntityEditorOutput {
public:
    EntityActions(EditorController &workspace, Panda::EditorLayer &app);

    // The entities an edit applies to: the selection — or, while an edit of a tab runs, the
    // entities of that tab (EditorTabs puts beginTarget / endTarget around the edit: a pinned tab
    // edits its own entities whatever is selected).
    std::vector<Panda::Entity> targets();
    Panda::Entity primaryTarget(); // the first of them; invalid — none
    std::vector<Panda::UUID> targetIds();
    void beginTarget(const std::vector<Panda::UUID> &ids);
    void endTarget();

    // The name of entities as one undoable edit (the field of the tab, Rename of the Hierarchy).
    void rename(const std::vector<Panda::UUID> &ids, const std::string &name);
    // Models become entities of the world: a subtree per mesh asset (a drop onto the viewport,
    // Add to World of the Content Browser).
    void instantiateMeshes(const std::vector<Panda::AssetId> &meshes);

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
    void entityDidEditScriptField(int scriptIndex, const std::string &fieldName, Panda::ScriptFieldValue value) override;
    void entityDidClickBackground() override;

private:
    Panda::World *world(); // the world the panels show; nullptr — none is open
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
    std::vector<Panda::UUID> m_target; // the entities of the tab whose edit runs
    bool m_targetActive = false;
    int m_nextEntity = 1; // counter for the names of new entities
};

} // namespace Editor
