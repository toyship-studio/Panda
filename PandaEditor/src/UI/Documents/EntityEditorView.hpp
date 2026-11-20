#pragma once

#include "UI/Widgets/PanelHeader.hpp"

#include <PandaEditorKit/EditorExtensions.hpp> // Editor::ComponentInspector
#include <PandaEditorKit/Widgets/EditorView.hpp>
#include <PandaEditorKit/Widgets/Picker.hpp>
#include <PandaEditorKit/Widgets/Vector3Field.hpp>

#include <Panda/Assets/Base/Asset.hpp> // Panda::AssetId
#include <Panda/GameLogic/Components/BaseComponents.hpp>
#include <Panda/GameLogic/Entity.hpp>
#include <Panda/Prefabs/Prefabs.hpp>
#include <Panda/ScriptEngine/ExternalScript.hpp> // Panda::ExternalScript, ScriptFieldValue

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Panda {
struct ComponentType;
}

namespace Editor {

// Intents of the entity editor — implemented by Editor::EntityActions. Edits apply to the targets
// of the tab (EditorTabs names them for the duration of an edit); identity
// is the engine's (a component by the name of its type, a script by its index in
// ScriptListComponent). The values of a component are edited by its section itself
// (ComponentInspector::build, Kit: commitComponent<T>).
class EntityEditorOutput {
public:
    virtual ~EntityEditorOutput() = default;
    virtual void entityDidEditName(const std::string &name) = 0;
    virtual void entityDidSetStatic(bool isStatic) = 0; // the Static flag in the header (IsStaticComponent tag)
    virtual void entityDidEditPosition(glm::vec3 position) = 0;
    virtual void entityDidEditRotation(glm::vec3 rotation) = 0;
    virtual void entityDidEditScale(glm::vec3 scale) = 0;
    // A component by the name of its type (EditorExtensions::componentInspectors).
    virtual void entityDidAddComponent(const std::string &component) = 0;
    virtual void entityDidRemoveComponent(const std::string &component) = 0;
    virtual void entityDidAddScript(const std::string &className) = 0;
    virtual void entityDidRemoveScript(int scriptIndex) = 0;
    // The new value of a field of a script. structural — the items of a list were added, removed
    // or moved: the edit is a step of the history of its own and the form is built again.
    virtual void entityDidEditScriptField(
        int scriptIndex, const std::string &fieldName, Panda::ScriptFieldValue value, bool structural
    ) = 0;
    virtual void entityDidClickBackground() = 0; // click on the background → clear selection
    // Prefabs (context/prefabs.md), for the primary target — a part of an instance. What it
    // differs in from its prefab is taken back (Revert) or written into the prefab (Apply;
    // forced — taken out of the other instances too). `whole` — the whole instance the entity is
    // a part of; else the entity itself, a component of it (the name of its type), or a place
    // of that component (Prefabs::Scope::field).
    virtual void entityDidRevertPrefab(bool whole, const std::string &component, const std::string &field) = 0;
    virtual void entityDidApplyPrefab(bool whole, const std::string &component, const std::string &field, bool forced) = 0;
    virtual void entityDidRestoreRemoved(Panda::UUID source) = 0; // an entity of the prefab the instance removed
    virtual void entityDidOpenPrefab() = 0;                       // the prefab, in its tab
    virtual void entityDidSelectPrefabRoot() = 0;                 // the root of the instance
    virtual void entityDidSelectEntity(Panda::UUID id) = 0;        // a link of the form that names another entity
    // An addition to an instance (Prefabs::Addition::top): out of the instance, or into its prefab.
    virtual void entityDidRevertAddition(Panda::UUID top) = 0;
    virtual void entityDidApplyAddition(Panda::UUID top) = 0;
};

// VIEW: the form of an entity tab (the document model: an entity tab is a view of the world
// document). setEntities shows the engine components of the targets, read directly; with
// several targets the primary one gives the values and differing fields are marked "mixed".
// The name, the transform and the scripts are the form's own; every component section comes
// from the EditorExtensions::componentInspectors registry, the core's like the modules'.
class EntityEditorView final : public EditorView {
public:
    explicit EntityEditorView(EntityEditorOutput *output);

    // Input: the targets of the tab — engine entities (live; empty — the placeholder).
    void setEntities(const std::vector<Panda::Entity> &entities);
    // PLAY mode: show the "edits go to the runtime world" banner (set by the coordinator when the
    // mode changes).
    void setRuntimeMode(bool runtime);
    // A light refresh of ONLY the Transform fields from the engine (current targets) — on an
    // external edit (gizmo drag), without rebuilding sections.
    void refreshTransform();
    // Classes for PickScript — the provider is called LIVE when the picker opens.
    void setScriptClassProvider(std::function<std::vector<std::string>()> provider);
    // The frame: what an instance differs in from its prefab follows the edits of the world.
    void update(double deltaTime) override;

private:
    enum class Mode { Empty, Entity };

    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    bool pointerDown(PandaUI::PointerEvent &event) override;

    void wireEditing();
    void showMode(Mode mode);
    void bindEntities(const std::vector<Panda::Entity> &entities);
    void rebuildComponents(Panda::Entity primary); // component sections from engine

    // The section of a component: the header and its menu are the form's, the content is the
    // registry's.
    std::shared_ptr<PandaUI::CollapsibleSection>
    buildComponentSection(const ComponentInspector &inspector, Panda::Entity primary);
    std::shared_ptr<PandaUI::CollapsibleSection> buildScriptSection(Panda::ExternalScript &script, int scriptIndex);
    // The field of a script of the primary target as the world holds it now; nullptr — it is gone.
    Panda::ScriptField *scriptField(int scriptIndex, const std::string &fieldName);

    // "..." in the section header → the menu of the component: Copy Values, Paste Values and Reset
    // for a component with a text form (type — its entry in the registry of the engine; nullptr —
    // none), and Remove Component (onRemove defines what to remove).
    void addComponentHeaderMenu(
        PandaUI::CollapsibleSection &section, std::function<void()> onRemove, const Panda::ComponentType *type = nullptr
    );
    // ---- Prefabs: what the primary target differs in from its prefab, shown.
    // The strip under the name: the prefab, how much differs, Revert and Apply, and what no row
    // of the form can show (a component or an entity the instance took off).
    void rebuildPrefabStrip();
    void readDifference(); // m_difference and m_addedTo of the primary target
    // The marks of the rows and of the sections, from m_difference; the strip with them.
    void showDifference();
    // The rows of a section whose fields differ are marked; a field that has no row of its name
    // is named in a line of the section.
    // The rows of a section by the fields they name: mixed among the targets, the instance's own.
    struct MarkedSection;
    void markSection(MarkedSection &marked);
    void showDifferenceMenu(PandaUI::Point at, const std::string &component, const std::string &field);
    // Runs at the start of the next tick, if the form is still there: what a button of the form
    // asks for rebuilds the form, and a control is not destroyed from its own callback.
    void later(std::function<void()> action);
    void openScriptPicker(PandaUI::View *anchor, std::function<void(const std::string &)> onPick);
    void openAddComponent(); // the picker of the components the targets can get

    EntityEditorOutput *m_output;
    bool m_binding = false; // widget edits during binding do not go to output

    std::shared_ptr<PandaUI::Label> m_runtimeBanner; // PLAY mode banner
    std::vector<Panda::Entity> m_entities;           // current targets (for highlighting mixed fields)
    std::function<std::vector<std::string>()> m_scriptClassProvider;

    std::shared_ptr<PandaUI::ScrollView> m_body;
    std::shared_ptr<PandaUI::TextField> m_name;
    std::shared_ptr<PandaUI::Label> m_staticLabel; // "* Static" for a mixed selection
    std::shared_ptr<PandaUI::Toggle> m_static;
    std::shared_ptr<Vector3Field> m_position;
    std::shared_ptr<Vector3Field> m_rotation;
    std::shared_ptr<Vector3Field> m_scale;
    std::shared_ptr<PandaUI::PropertyRow> m_positionRow;
    std::shared_ptr<PandaUI::PropertyRow> m_rotationRow;
    std::shared_ptr<PandaUI::PropertyRow> m_scaleRow;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true); // the lifetime token of `later`
    std::shared_ptr<PandaUI::Panel> m_prefabStrip; // under the name; collapsed for a plain entity
    Panda::Prefabs::Difference m_difference;       // of the primary target
    // The primary target is the topmost entity of an addition to an instance
    // (Prefabs::Addition): the root of that instance; 0 — it is not.
    Panda::UUID m_addedTo = 0;
    uint64_t m_differenceRevision = 0; // the revision of the world's history they were read at
    double m_sinceRead = 0.0;          // seconds since they were read
    // The sections that show a component: its type, the path of its place in the difference
    // (a script: `scripts/<Class>/scriptFields`), the section, and the line that names what
    // differs without a row.
    struct MarkedSection {
        std::string component;
        std::string prefix;
        PandaUI::CollapsibleSection *section = nullptr;
        std::shared_ptr<PandaUI::Label> note;
    };
    std::vector<MarkedSection> m_markedSections;
    std::shared_ptr<PandaUI::Panel> m_componentsContainer;
    std::shared_ptr<PandaUI::Button> m_addComponent;
    // What Add Component offers the current targets: a row of its picker and what choosing it does.
    struct Addable {
        PickerItem item;
        std::function<void()> run;
    };
    std::vector<Addable> m_addable;

    // Empty state.
    std::shared_ptr<PandaUI::Panel> m_placeholder;
};

} // namespace Editor
