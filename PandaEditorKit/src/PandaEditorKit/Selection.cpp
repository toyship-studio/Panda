#include "PandaEditorKit/Selection.hpp"

#include <Panda/Assets/Base/AssetHandler.hpp>
#include <Panda/GameLogic/GameContext.hpp>

#include <utility>

namespace Editor {

Selection &selection() {
    static Selection instance;
    return instance;
}

void Selection::setWorld(Panda::World *world) {
    m_world = world;
    if (world == nullptr) {
        deselectEntities();
        return;
    }
    forgetDeadEntities();
    transformChanged(); // the same entities, where the new world has them
}

std::vector<Panda::Entity> Selection::entities() const {
    std::vector<Panda::Entity> result;
    if (m_world == nullptr) { return result; }
    result.reserve(m_entities.size());
    for (Panda::UUID id : m_entities) {
        Panda::Entity entity = m_world->getById(id);
        if (entity.isValid()) { result.push_back(entity); }
    }
    return result;
}

bool Selection::insertEntity(Panda::UUID id) {
    if (m_world == nullptr || !m_world->isValid(id)) { return false; }
    return m_entities.insert(id).second;
}

void Selection::selectEntities(const std::vector<Panda::UUID> &ids) {
    std::unordered_set<Panda::UUID> selected;
    if (m_world != nullptr) {
        for (Panda::UUID id : ids) {
            if (m_world->isValid(id)) { selected.insert(id); }
        }
    }
    if (selected == m_entities && m_assets.empty()) { return; }
    m_entities = std::move(selected);
    m_assets.clear();
    ++m_version;
    transformChanged();
}

void Selection::addEntities(const std::unordered_set<Panda::UUID> &ids) {
    bool added = false;
    for (Panda::UUID id : ids) { added |= insertEntity(id); }
    if (!added) { return; }
    m_assets.clear();
    ++m_version;
    transformChanged();
}

void Selection::removeEntities(const std::unordered_set<Panda::UUID> &ids) {
    bool removed = false;
    for (Panda::UUID id : ids) { removed |= m_entities.erase(id) > 0; }
    if (!removed) { return; }
    ++m_version;
    transformChanged();
}

void Selection::deselectEntities() {
    if (m_entities.empty()) { return; }
    m_entities.clear();
    ++m_version;
    transformChanged();
}

void Selection::forgetDeadEntities() {
    const size_t before = m_entities.size();
    std::erase_if(m_entities, [this](Panda::UUID id) { return m_world == nullptr || !m_world->isValid(id); });
    if (m_entities.size() == before) { return; }
    ++m_version;
    transformChanged();
}

void Selection::selectAssets(std::vector<Panda::AssetId> ids) {
    if (ids == m_assets) { return; } // an empty selection over an entity selection changes nothing
    m_assets = std::move(ids);
    ++m_version;
    if (!m_assets.empty() && !m_entities.empty()) {
        m_entities.clear();
        transformChanged();
    }
}

void Selection::forgetDeadAssets() {
    Panda::AssetHandler *handler = Panda::GameContext::getAssetHandler();
    const size_t before = m_assets.size();
    std::erase_if(m_assets, [handler](Panda::AssetId id) { return handler == nullptr || !handler->hasAsset(id); });
    if (m_assets.size() != before) { ++m_version; }
}

void Selection::clear() {
    if (m_entities.empty() && m_assets.empty()) { return; }
    m_assets.clear();
    ++m_version;
    if (!m_entities.empty()) {
        m_entities.clear();
        transformChanged();
    }
}

void Selection::resetMedian() {
    m_medianPosition = glm::vec3(0.f);
    m_medianMatrix = glm::mat4(1.f);
}

void Selection::transformChanged() {
    ++m_transformVersion;
    resetMedian();
    m_manipulated.clear();
    if (m_world == nullptr) { return; }

    glm::vec3 position(0.f);
    glm::vec3 scale(0.f);
    glm::quat rotation = glm::quat();
    int count = 0;
    for (Panda::UUID id : m_entities) {
        Panda::Entity entity = m_world->getById(id);
        if (!entity.isValid()) { continue; } // gone from the world; forgetDeadEntities takes it out
        // A selected entity under a selected ancestor moves with it.
        bool underSelected = false;
        for (Panda::Entity parent = entity.getParent(); parent.isValid(); parent = parent.getParent()) {
            if (m_entities.contains(parent.getId())) {
                underSelected = true;
                break;
            }
        }
        if (!underSelected) { m_manipulated.insert(id); }

        Panda::TransformComponent transform = entity.calculateWorldSpaceTransform();
        position += transform.getPosition();
        scale += transform.getScale();
        rotation = count == 0 ? transform.getRotation()
                              : glm::normalize(glm::slerp(rotation, transform.getRotation(), 0.5f));
        ++count;
    }
    if (count == 0) { return; }
    position /= static_cast<float>(count);
    scale /= static_cast<float>(count);
    m_medianPosition = position;
    m_medianMatrix =
        glm::translate(glm::mat4(1.f), position) * glm::toMat4(rotation) * glm::scale(glm::mat4(1.f), scale);
}

} // namespace Editor
