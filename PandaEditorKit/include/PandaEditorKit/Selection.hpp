#pragma once

#include <Panda/Assets/Base/Asset.hpp> // Panda::AssetId
#include <Panda/GameLogic/Entity.hpp>
#include <Panda/GameLogic/World.hpp>

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace Editor {

// What is selected in the editor: entities of the world it shows, or assets of the project — one
// or the other, never both. One object for the whole editor (selection()): the viewport picks
// into it, the gizmo and the outline read it, the panels and the tabs show it, the editor parts
// of modules ask it.
//
// Nobody is called back on a change: who shows the selection compares version() once a frame
// (transformVersion() — the selected entities moved under the same selection).
class Selection final {
public:
    // The world the selected entities belong to: the one the editor shows (the edited world, the
    // playing copy). The entities the new world does not have leave the selection; without a
    // world no entity is selected.
    void setWorld(Panda::World *world);

    // ---- Entities ----
    const std::unordered_set<Panda::UUID> &entityIds() const {
        return m_entities;
    }
    bool isSelected(Panda::UUID id) const {
        return m_entities.contains(id);
    }
    bool hasEntities() const {
        return !m_entities.empty();
    }
    std::vector<Panda::Entity> entities() const; // the selected ones the world still has
    // Instead of what was selected; an entity selection deselects the assets.
    void selectEntities(const std::vector<Panda::UUID> &ids);
    void addEntities(const std::unordered_set<Panda::UUID> &ids); // a rectangle, a click with Shift
    void removeEntities(const std::unordered_set<Panda::UUID> &ids);
    void deselectEntities();
    // The world lost entities (a deletion, its undo, a script): they leave the selection.
    void forgetDeadEntities();

    // ---- Assets ----
    const std::vector<Panda::AssetId> &assets() const {
        return m_assets;
    }
    // Instead of what was selected; an asset selection deselects the entities.
    void selectAssets(std::vector<Panda::AssetId> ids);

    void clear(); // nothing is selected

    // ---- The selected entities as one body (the gizmo) ----
    // The selected entities without the descendants of selected ones: a parent moves its children.
    const std::unordered_set<Panda::UUID> &manipulatedEntityIds() const {
        return m_manipulated;
    }
    const glm::mat4 &medianMatrix() const {
        return m_medianMatrix;
    }
    glm::vec3 medianPosition() const {
        return m_medianPosition;
    }
    // The selected entities were moved (the gizmo, the inspector, an undo, the running game): the
    // median is found again.
    void transformChanged();

    // Counts the changes of what is selected.
    std::uint64_t version() const {
        return m_version;
    }
    // Counts transformChanged: the Transform fields of the inspector and the gizmo follow each other.
    std::uint64_t transformVersion() const {
        return m_transformVersion;
    }

private:
    bool insertEntity(Panda::UUID id);
    void resetMedian();

    Panda::World *m_world = nullptr;
    std::unordered_set<Panda::UUID> m_entities;
    std::unordered_set<Panda::UUID> m_manipulated;
    std::vector<Panda::AssetId> m_assets;
    glm::vec3 m_medianPosition = glm::vec3(0.f);
    glm::mat4 m_medianMatrix = glm::mat4(1.f);
    std::uint64_t m_version = 0;
    std::uint64_t m_transformVersion = 0;
};

// The selection of the editor (one per process).
Selection &selection();

} // namespace Editor
