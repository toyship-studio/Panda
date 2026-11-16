#pragma once

#include "UI/Widgets/PanelHeader.hpp"

#include <PandaEditorKit/EditorExtensions.hpp> // Editor::ComponentInspector
#include <PandaEditorKit/Widgets/EditorView.hpp>
#include <PandaEditorKit/Widgets/Picker.hpp>
#include <PandaEditorKit/Widgets/Vector3Field.hpp>

#include <Panda/Assets/Base/Asset.hpp> // Panda::AssetId
#include <Panda/GameLogic/Components/BaseComponents.hpp>
#include <Panda/GameLogic/Entity.hpp>
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
