#pragma once

#include "UI/EditorModelHelper.hpp"
#include "UI/Widgets/PanelHeader.hpp"

#include <PandaEditorKit/EditorExtensions.hpp> // Editor::ComponentInspector (module component sections)
#include <PandaEditorKit/Widgets/EditorView.hpp>
#include <PandaEditorKit/Widgets/Vector3Field.hpp>

#include <Panda/Assets/Base/Asset.hpp>                   // Panda::AssetId
#include <Panda/GameLogic/Components/BaseComponents.hpp> // engine components (edit payload)
#include <Panda/GameLogic/Entity.hpp>
#include <Panda/ScriptEngine/ExternalScript.hpp> // Panda::ExternalScript, ScriptFieldValue

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace Editor {

// Payload of a CORE component edit — a concrete engine component (the View edits a copy and sends it
// as a whole; the coordinator writes to the World via GenericComponentUpdate<T> with a single
// visitor). No intermediate model. Module components edit themselves (Kit: commitComponent<T>).
using ComponentEdit =
    std::variant<Panda::CameraComponent, Panda::DirectionalLightComponent, Panda::PointLightComponent,
                 Panda::SpriteRendererComponent, Panda::RuntimeMeshComponent, Panda::PrimitiveMeshComponent,
                 Panda::ImportedSubmeshComponent, Panda::SkyComponent>;

// Intents of the entity editor — implemented by the coordinator. Edits apply to the targets of the
// tab (the coordinator substitutes the selection with them for the duration of an edit); identity
// is the engine's (a component by ComponentType, a script by its index in ScriptListComponent).
class EntityEditorOutput {
public:
    virtual ~EntityEditorOutput() = default;
    virtual void entityDidEditName(const std::string &name) = 0;
    virtual void entityDidSetStatic(bool isStatic) = 0; // the Static flag in the header (IsStaticComponent tag)
    virtual void entityDidEditPosition(glm::vec3 position) = 0;
    virtual void entityDidEditRotation(glm::vec3 rotation) = 0;
    virtual void entityDidEditScale(glm::vec3 scale) = 0;
    virtual void entityDidAddComponent(ComponentType type) = 0;
    virtual void entityDidRemoveComponent(ComponentType type) = 0;
    virtual void entityDidEditComponent(const ComponentEdit &edit) = 0; // edit of one component (visit → command)
    // A module component by type name (EditorExtensions::componentInspectors): add/remove.
    virtual void entityDidAddExtensionComponent(const std::string &component) = 0;
    virtual void entityDidRemoveExtensionComponent(const std::string &component) = 0;
    virtual void entityDidAddScript(const std::string &className) = 0;
    virtual void entityDidRemoveScript(int scriptIndex) = 0;
    virtual void entityDidEditScriptField(int scriptIndex, const std::string &fieldName, Panda::ScriptFieldValue value) = 0;
    virtual void entityDidClickBackground() = 0; // click on the background → clear selection
    // A click on an AssetWell value: select the asset in the editor (its editor tab opens).
    virtual void entityDidSelectAsset(Panda::AssetId assetId) = 0;
    virtual void entityDidRequestMaximize() = 0; // zoom overlay of the dock of the tab
};

// VIEW: the form of an entity tab (the document model: an entity tab is a view of the world
// document). setEntities shows the engine components of the targets, read directly; with
// several targets the primary one gives the values and differing fields are marked "mixed".
// Edits are sent through the Output in engine terms. Component sections of the core are built
// here; module components come from the EditorExtensions::componentInspectors registry.
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

    // Per-type sections read/write the engine component (a copy) and send ComponentEdit.
    std::shared_ptr<PandaUI::CollapsibleSection> buildCameraSection(const Panda::CameraComponent &component);
    std::shared_ptr<PandaUI::CollapsibleSection>
    buildDirectionalLightSection(const Panda::DirectionalLightComponent &component);
    std::shared_ptr<PandaUI::CollapsibleSection> buildPointLightSection(const Panda::PointLightComponent &component);
    std::shared_ptr<PandaUI::CollapsibleSection> buildSpriteRendererSection(const Panda::SpriteRendererComponent &component);
    std::shared_ptr<PandaUI::CollapsibleSection> buildRuntimeMeshSection(const Panda::RuntimeMeshComponent &component);
    std::shared_ptr<PandaUI::CollapsibleSection> buildPrimitiveMeshSection(const Panda::PrimitiveMeshComponent &component);
    std::shared_ptr<PandaUI::CollapsibleSection>
    buildImportedSubmeshSection(const Panda::ImportedSubmeshComponent &component);
    std::shared_ptr<PandaUI::CollapsibleSection> buildSkySection(const Panda::SkyComponent &component);
    // A module component section: the header and the Remove menu are the form's, the content is
    // a registry contribution.
    std::shared_ptr<PandaUI::CollapsibleSection>
    buildExtensionSection(const ComponentInspector &inspector, Panda::Entity primary);
    std::shared_ptr<PandaUI::CollapsibleSection> buildScriptSection(Panda::ExternalScript &script, int scriptIndex);

    // An AssetRef slot: drop of an asset of the given kind + click to clear; the name is resolved
    // from the project registry.
    void addAssetRow(PandaUI::CollapsibleSection &section, const char *title, const char *kind, uint32_t value,
                     std::function<void(uint32_t)> set);
    // "..." in the section header → the "Remove Component" menu (onRemove defines what to remove).
    void addComponentHeaderMenu(PandaUI::CollapsibleSection &section, std::function<void()> onRemove);
    void openScriptPicker(PandaUI::View *anchor, std::function<void(const std::string &)> onPick);
    std::string assetNameFor(uint32_t id) const; // id → asset name from the registry (or empty)

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
    std::shared_ptr<PandaUI::MenuButton> m_addComponent;

    // Empty state.
    std::shared_ptr<PandaUI::Panel> m_placeholder;
};

} // namespace Editor
