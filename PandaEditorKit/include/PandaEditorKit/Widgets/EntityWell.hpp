#pragma once

#include <Panda/Base/UUID.hpp>
#include <PandaUI/PandaUI.hpp>

#include <functional>
#include <memory>

namespace Panda {
class World;
}

namespace Editor {

// A slot referencing an entity of the world (joints: connectedEntity). Accepts an "entity" drop
// from the Hierarchy. A click on the value shows where the entity is (the Hierarchy opens its
// ancestors and blinks the row; the selection stays), a double click selects it; the reference
// is cleared by its own button, never by a click on the value. The name is resolved from the
// world (getById → getName).
class EntityWell final : public PandaUI::Control {
public:
    EntityWell(Panda::World &world, Panda::UUID initial, std::function<void(Panda::UUID)> onChange);

    bool canAcceptDrop(const PandaUI::DragInfo &drag) override;
    void dragEntered(const PandaUI::DragInfo &drag) override;
    void dragExited() override;
    bool performDrop(const PandaUI::DragInfo &drag) override;
    void themeChanged() override;

protected:
    bool pointerDown(PandaUI::PointerEvent &event) override; // notes a double click
    void performPrimaryAction() override;                    // a click reveals, a double click selects

private:
    void setValue(Panda::UUID id); // from a drop or the clear button: refresh + onChange
    void refresh();
    void applyStyle();

    Panda::World *m_world;
    Panda::UUID m_entityId;
    std::function<void(Panda::UUID)> m_onChange;
    std::shared_ptr<PandaUI::Label> m_label;
    std::shared_ptr<PandaUI::View> m_clearButton;
    bool m_dropHovered = false;
    bool m_doubleClick = false; // the press that started this click was the second of a double click
};

} // namespace Editor
