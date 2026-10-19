#pragma once

#include <PandaEditorKit/Widgets/EditorView.hpp>
#include "UI/Widgets/PanelHeader.hpp"

#include <Panda/Base/UUID.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Panda {
class World;
} // namespace Panda

namespace Editor {

// Intents of the hierarchy panel — implemented by the coordinator (EditorController). The identity
// of entities is the engine UUID (0 = the synthetic "Scene" root). No reactive machinery: explicit
// named calls.
enum class PrimitiveKind { Cube, Sphere, Capsule, Plane, Cylinder };

class HierarchyPanelOutput {
public:
    virtual ~HierarchyPanelOutput() = default;
    virtual void hierarchyDidSelectEntities(const std::vector<Panda::UUID> &entityIds) = 0; // empty = clear selection
    virtual void hierarchyDidRequestAddChild(Panda::UUID parentId) = 0;                     // 0 = at the scene root
    // A greyboxing primitive: an entity with a builtin mesh, a checker material and a collider.
    virtual void hierarchyDidRequestAddPrimitive(Panda::UUID parentId, PrimitiveKind kind) = 0;
    virtual void hierarchyDidRequestDuplicate(const std::vector<Panda::UUID> &entityIds) = 0;
    virtual void hierarchyDidRequestDelete(const std::vector<Panda::UUID> &entityIds) = 0;
    virtual void hierarchyDidReparent(const std::vector<Panda::UUID> &itemIds, Panda::UUID newParentId) = 0; // 0 = to root
    virtual void hierarchyDidRequestMaximize() = 0; // zoom overlay of the left dock (chrome)
};

// VIEW: the hierarchy panel. The tree is read directly from the engine World (TreeItem = UUID), and
// intents are sent outward through HierarchyPanelOutput. Contains no model mutations.
class HierarchyPanelView final : public EditorView {
public:
    explicit HierarchyPanelView(HierarchyPanelOutput *output);

    // Input.
    void setWorld(Panda::World *world); // tree source; reload() after structural changes
    void reload();                      // re-read the tree (recomputes the root entities)
    void setRootTitle(std::string title); // root name = the name of the open world file
    void setStatusText(const std::string &text);
    void setSelectedEntities(const std::vector<Panda::UUID> &entityIds); // programmatic highlight of the selection
    void expandEntity(Panda::UUID entityId, bool expanded);

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    bool pointerDown(PandaUI::PointerEvent &event) override; // click on the background → clear the selection
    bool keyDown(PandaUI::KeyEvent &event) override;         // Delete — remove, Cmd/Ctrl+D — duplicate

    void configureTree();
    void showContextMenu(PandaUI::TreeView &tree, Panda::UUID entityId, PandaUI::Point position);

    HierarchyPanelOutput *m_output;
    Panda::World *m_world = nullptr;
    std::string m_rootTitle = "World";
    std::vector<std::uint32_t> m_rootChildren; // root entities (without a parent), recomputed in reload()
    std::shared_ptr<PandaUI::TreeView> m_tree;
    std::shared_ptr<PandaUI::Label> m_status;
};

} // namespace Editor
