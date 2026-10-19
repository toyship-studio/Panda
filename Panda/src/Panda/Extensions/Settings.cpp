#include "Panda/Extensions/Settings.hpp"

#include <Foundation/Foundation.hpp>

namespace Panda {

void SettingsRegistry::add(SettingsType type) {
    PND_ASSERT(!type.name.empty(), "SETTINGS TYPE: THE NAME IS REQUIRED");
    PND_ASSERT_F(find(type.scope, type.name) == nullptr, "SETTINGS %s REGISTERED TWICE", type.name.c_str());
    // The world file keeps its entities under this key next to the sections (WorldMapper).
    PND_ASSERT(!(type.scope == SettingsScope::World && type.name == "entities"), "SETTINGS TYPE: \"entities\" IS RESERVED");
    const bool complete = type.encode != nullptr && type.decode != nullptr && type.copy != nullptr && type.reset != nullptr;
    PND_ASSERT(complete, "SETTINGS TYPE: ALL HOOKS REQUIRED");
    m_types.push_back(std::move(type));
}

const SettingsType *SettingsRegistry::find(SettingsScope scope, std::string_view name) const {
    for (const SettingsType &type : m_types) {
        if (type.scope == scope && type.name == name) { return &type; }
    }
    return nullptr;
}

} // namespace Panda
