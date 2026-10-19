#pragma once

#include "PandaEditorKit/Document.hpp"
#include "PandaEditorKit/Viewport/HelperLine.hpp"

#include <Foundation/Memory.hpp>
#include <Miren/Miren.hpp>
#include <Panda/Assets/Base/Asset.hpp>
#include <Panda/Assets/Base/Path.hpp>
#include <Panda/Assets/MaterialAsset.hpp>
#include <Panda/Extensions/Settings.hpp>
#include <Panda/GameLogic/Entity.hpp>
#include <Panda/Renderer/Vertex.hpp>
#include <PandaUI/PandaUI.hpp>

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Panda {
class Camera;
class World;
} // namespace Panda

namespace Editor {

// Editor registries (context/plugins.md, stage 2): extension points through which the editor
// core learns about the contributions of the editor parts of modules (registered when a project
// is opened, according to its set). Contributions are not unloaded. The owner is editorExtensions().

// ---- Inspector section for a component ----

struct ComponentInspectorContext {
    Panda::World &world;
    // Form targets: the first is primary (section values), an edit hits all that have the component.
    const std::vector<Panda::Entity> &entities;
};

struct ComponentInspector final {
    std::string component;   // type name in the engine registry ("terrain2dComponent"): presence on an entity — by it
    std::string displayName; // "Terrain 2D": the Add Component item; the section title — in caps
    std::string icon;        // icon of an entity with this component (Hierarchy, tabs); empty — the regular one
    // Add/remove the component on entities with one undo command (Kit: toggleComponentPresence<T>).
    void (*setPresence)(Panda::World &world, const std::vector<Panda::Entity> &entities, bool add) = nullptr;
    // Section content: edits a copy of the component and writes it to all targets
    // (Kit: commitComponent<T>).
    void (*build)(PandaUI::CollapsibleSection &section, const ComponentInspectorContext &context) = nullptr;
};

class ComponentInspectorRegistry final {
public:
    void add(ComponentInspector inspector);
    const std::vector<ComponentInspector> &inspectors() const {
        return m_inspectors;
    }
    const ComponentInspector *find(std::string_view component) const;
    // Whether an entity has the component — by the typeId of the engine registry (the name is the
    // only identity).
    bool has(const ComponentInspector &inspector, Panda::Entity entity) const;

private:
    std::vector<ComponentInspector> m_inspectors;
};

// ---- Asset kind in the editor: icon, color, form tab ----

struct AssetEditorContext {
    Panda::AssetId asset = 0;
    std::function<void()> rebuild; // rebuild the form at the next tick start (the section set depends on the values)
    std::function<void()> edited;  // the document was changed from the form: the preview tab gets pinned
};

// Asset form tab: an instance per (panel, asset), survives form rebuilds — the state
// (selected list rows) lives in it; dies with the tab.
class AssetEditor {
public:
    virtual ~AssetEditor() = default;
    virtual void build(PandaUI::View &container) = 0; // the container is already empty
};

struct AssetKindUi final {
    std::string kind; // AssetKind::name ("terrain2d")
    std::string icon; // icon in the Content Browser grid and on tabs; empty — the generic file
    PandaUI::Color color = PandaUI::Color(0x8A8A8AFF); // icon color in the grid
    std::unique_ptr<AssetEditor> (*createEditor)(const AssetEditorContext &context) = nullptr; // nullptr — no form
};

class AssetKindUiRegistry final {
public:
    void add(AssetKindUi kind);
    const std::vector<AssetKindUi> &kinds() const {
        return m_kinds;
    }
    const AssetKindUi *find(std::string_view kind) const;

private:
    std::vector<AssetKindUi> m_kinds;
};

// ---- "New …" of the Content Browser context menu ----

struct AssetCreator final {
    std::string title; // "New Terrain"
    // Create a file in the project directory; return its path (empty — an error). The core registers
    // and selects the new asset.
    Panda::path_t (*create)(const Panda::path_t &directory) = nullptr;
};

// A free path <directory>/<baseName><extension>; if taken — "<baseName> 2", "<baseName> 3", …
Panda::path_t uniqueAssetPath(const Panda::path_t &directory, const std::string &baseName, const std::string &extension);

// ---- Viewport tool (EDIT) ----

struct ViewportToolFrame {
    Panda::World &world;   // the world being edited
    Panda::Camera &camera; // viewport camera
    glm::mat4 view;
    glm::mat4 viewProj;
    glm::vec3 cameraPosition;
    bool viewportHovered;
    double deltaTime;
};

class ViewportTool {
public:
    virtual ~ViewportTool() = default;
    // The frame — before the world update: data edits are remeshed in the same frame.
    virtual void update(const ViewportToolFrame &frame) = 0;
    // The tool claims the viewport LMB: rect-select and the gizmo are disabled.
    virtual bool isDriving() const = 0;
    // Lines on top of the viewport (the brush halo etc.) — in window coordinates.
    virtual void appendHelperLines(
        std::vector<Panda::HelperLine> &lines, const glm::mat4 &viewProj, PandaUI::Rect viewportFrame
    ) const {
        (void)lines;
        (void)viewProj;
        (void)viewportFrame;
    }
};

class ViewportToolRegistry final {
public:
    ViewportTool &add(std::unique_ptr<ViewportTool> tool); // owns it; the reference lives until the process ends
    const std::vector<std::unique_ptr<ViewportTool>> &tools() const {
        return m_tools;
    }
    bool anyDriving() const;

private:
    std::vector<std::unique_ptr<ViewportTool>> m_tools;
};

// ---- Viewport overlay: helper lines over the world the viewport shows ----

// Once a frame in EDIT and SIMULATE, with the editor camera, over the world the viewport shows
// (the one being edited, or the playing one in SIMULATE; nothing in PLAY — the view is the
// game's). The overlay decides itself whether it has anything to show (its editor-scope setting).
// The lines are in window coordinates (HelperLine) and go in the order of importance: the viewport
// draws the head of the list.
struct ViewportOverlay final {
    std::string name; // "colliders"
    void (*appendHelperLines)(
        Panda::World &world, std::vector<Panda::HelperLine> &lines, const glm::mat4 &viewProj, PandaUI::Rect viewportFrame
    ) = nullptr;
};

// ---- Settings section over the sections of a scope (Extensions::settings) ----

struct SettingsSection final {
    Panda::SettingsScope scope = Panda::SettingsScope::World; // whose store the section edits
    std::string title;                                        // "TERRAIN" (in caps)
    // Content: reads and edits the settings in the store of the scope (the world: its context;
    // the project and the editor: the stores of their files); rebuilt when the store changes.
    void (*build)(PandaUI::CollapsibleSection &section, Panda::TypeContext &store) = nullptr;
};

// ---- Material preview (the sphere of panels and thumbnails) for the contribution's shaders ----

struct MaterialPreview final {
    // The material falls under the rule: by the id of the contribution's shader or by its field
    // convention. Rules are checked in registration order, the first matching one decides.
    bool (*matches)(Panda::MaterialAsset &material) = nullptr;
    // Sphere vertex color: the contribution's shader may read it as data (terrain layer weights).
    Panda::Color vertexColor = Panda::Color(1.f, 1.f, 1.f, 1.f);
    // Custom vertex format: the layout (lives with the static resources — taken when the sphere is
    // built) and the packing of the sphere vertices into it; nullptr — the common Vertex layout.
    Miren::VertexLayoutHandle (*vertexLayout)() = nullptr;
    Foundation::Memory (*packVertices)(const std::vector<Panda::Vertex> &vertices, uint32_t &outSize) = nullptr;
    void (*bindInputs)() = nullptr; // placeholder shader inputs before the sphere submit (splatmap)
};

// ---- Statistics panel section ----

struct StatisticsSection final {
    std::string title;             // "FLORA"
    std::vector<std::string> rows; // row captions
    void (*collect)(uint32_t *values) = nullptr; // once per frame: row values (rows.size())
};

class EditorExtensions final {
public:
    EditorExtensions() = default;
    EditorExtensions(const EditorExtensions &) = delete;
    EditorExtensions &operator=(const EditorExtensions &) = delete;

    ComponentInspectorRegistry componentInspectors;
    AssetKindUiRegistry assetKinds;
    std::vector<AssetCreator> assetCreators;
    ViewportToolRegistry viewportTools;
    std::vector<ViewportOverlay> viewportOverlays;
    std::vector<SettingsSection> settingsSections;
    DocumentTypeRegistry documentTypes; // the documents of the dock's "+" menu (Document.hpp)
    std::vector<StatisticsSection> statistics;
    std::vector<MaterialPreview> materialPreviews;
    // PNG icon folders (icon name = file name) relative to the application resources: a module's
    // resources/<name> folder gets into the editor bundle at build time (panda_package()).
    std::vector<Panda::path_t> iconFolders;
};

EditorExtensions &editorExtensions();

} // namespace Editor
