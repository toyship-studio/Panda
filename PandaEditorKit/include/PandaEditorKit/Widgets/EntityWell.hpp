#pragma once

#include <Panda/Base/UUID.hpp>
#include <PandaUI/PandaUI.hpp>

#include <functional>
#include <memory>

namespace Panda {
class World;
}

namespace Editor {

// A field referencing an entity of the world (a joint's connected entity, an entity field of a
// script): [icon + name | choose | clear] — the anatomy of AssetWell. The entity is chosen from
// a picker of the entities of the world (a click on an empty field or the "choose" button) or
// dropped from the Hierarchy. A click on the value shows where the entity is (the Hierarchy
// opens its ancestors and blinks the row; the selection stays), a double click selects it; the
// reference is cleared by its own button, never by a click on the value. The name is resolved
// from the world (getById → getName).
//
// A field that takes a prefab too (setAcceptsPrefabs — an entity field of a script: what it
// names is something to make copies of) holds the id of the prefab asset then: a prefab is
// dropped from the Content Browser or chosen from the picker, and the well shows its file.
class EntityWell final : public PandaUI::Panel {
public:
    EntityWell(Panda::World &world, Panda::UUID initial, std::function<void(Panda::UUID)> onChange);

    void setAcceptsPrefabs(bool accepts);

    bool canAcceptDrop(const PandaUI::DragInfo &drag) override;
    void dragEntered(const PandaUI::DragInfo &drag) override;
    void dragExited() override;
    bool performDrop(const PandaUI::DragInfo &drag) override;
    // The value is the name the well shows: the entity, or that there is none.
    void describeAccessibility(PandaUI::Accessibility &accessibility) const override;
    void themeChanged() override;

private:
    class MainZone;

    void setValue(Panda::UUID id); // from a drop, the picker or the clear button: refresh + onChange
    void openPicker();
    void refresh();
    void applyStyle();

    Panda::World *m_world;
    Panda::UUID m_entityId;
    std::function<void(Panda::UUID)> m_onChange;
    std::shared_ptr<MainZone> m_main;
    std::shared_ptr<PandaUI::ImageView> m_icon;
    std::shared_ptr<PandaUI::Label> m_label;
    std::shared_ptr<PandaUI::View> m_clearButton;
    bool m_dropHovered = false;
    bool m_acceptsPrefabs = false;
    bool isPrefab() const; // the value names a prefab asset, not an entity of the world
};

} // namespace Editor
