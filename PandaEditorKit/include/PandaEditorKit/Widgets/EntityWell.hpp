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
// from the Hierarchy; a click clears it; the name is resolved from the world (getById → getName).
class EntityWell final : public PandaUI::Control {
public:
    EntityWell(Panda::World &world, Panda::UUID initial, std::function<void(Panda::UUID)> onChange);

    bool canAcceptDrop(const PandaUI::DragInfo &drag) override;
    void dragEntered(const PandaUI::DragInfo &drag) override;
    void dragExited() override;
    bool performDrop(const PandaUI::DragInfo &drag) override;
    void themeChanged() override;

protected:
    void performPrimaryAction() override; // a click clears the reference

private:
    void refresh();
    void applyStyle();

    Panda::World *m_world;
    Panda::UUID m_entityId;
    std::function<void(Panda::UUID)> m_onChange;
    std::shared_ptr<PandaUI::Label> m_label;
    bool m_dropHovered = false;
};

} // namespace Editor
