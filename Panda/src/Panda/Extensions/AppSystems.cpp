#include "Panda/Extensions/AppSystems.hpp"

#include <Foundation/Foundation.hpp>

namespace Panda {

void AppSystemRegistry::add(AppSystem system) {
    PND_ASSERT(!system.name.empty(), "APP SYSTEM: name REQUIRED");
    for (const AppSystem &existing : m_systems) {
        PND_ASSERT_F(existing.name != system.name, "APP SYSTEM %s REGISTERED TWICE", system.name.c_str());
    }
    m_systems.push_back(std::move(system));
    const AppSystem &added = m_systems.back();
    if (m_live && added.start != nullptr) { added.start(); }
}

void AppSystemRegistry::setLive(bool live) {
    if (m_live == live) { return; }
    m_live = live;
    if (live) {
        for (const AppSystem &system : m_systems) {
            if (system.start != nullptr) { system.start(); }
        }
    } else {
        for (auto it = m_systems.rbegin(); it != m_systems.rend(); ++it) {
            if (it->stop != nullptr) { it->stop(); }
        }
    }
}

void AppSystemRegistry::tick(double deltaTime) {
    for (const AppSystem &system : m_systems) {
        if (system.tick != nullptr) { system.tick(deltaTime); }
    }
}

} // namespace Panda
