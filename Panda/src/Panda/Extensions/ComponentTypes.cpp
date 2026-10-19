#include "Panda/Extensions/ComponentTypes.hpp"

#include <Foundation/Foundation.hpp>

namespace Panda {

void ComponentTypeRegistry::add(ComponentType type) {
    PND_ASSERT_F(find(type.name) == nullptr, "COMPONENT TYPE %s REGISTERED TWICE", type.name.c_str());
    PND_ASSERT_F(find(type.typeId) == nullptr, "COMPONENT TYPE %s: entt TYPE REGISTERED TWICE", type.name.c_str());
    PND_ASSERT(type.prepare != nullptr, "COMPONENT TYPE WITHOUT prepare");
    m_types.push_back(std::move(type));
}

const ComponentType *ComponentTypeRegistry::find(std::string_view name) const {
    for (const ComponentType &type : m_types) {
        if (type.name == name) { return &type; }
    }
    return nullptr;
}

const ComponentType *ComponentTypeRegistry::find(entt::id_type typeId) const {
    for (const ComponentType &type : m_types) {
        if (type.typeId == typeId) { return &type; }
    }
    return nullptr;
}

void ComponentTypeRegistry::prepare(entt::registry &registry) const {
    for (const ComponentType &type : m_types) {
        type.prepare(registry);
    }
}

} // namespace Panda
