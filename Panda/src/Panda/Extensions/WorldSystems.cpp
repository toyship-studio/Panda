#include "Panda/Extensions/WorldSystems.hpp"

#include <Foundation/Foundation.hpp>

#include <algorithm>

namespace Panda {

namespace {

    bool contains(const std::vector<std::string> &names, const std::string &name) {
        return std::find(names.begin(), names.end(), name) != names.end();
    }

} // namespace

void WorldSystemRegistry::add(WorldSystem system) {
    for (const WorldSystem &known : m_systems) {
        PND_ASSERT_F(known.name != system.name, "WORLD SYSTEM %s REGISTERED TWICE", system.name.c_str());
    }
    m_systems.push_back(std::move(system));
    sort();
}

const WorldSystem *WorldSystemRegistry::find(const std::string &name) const {
    for (const WorldSystem &system : m_systems) {
        if (system.name == name) { return &system; }
    }
    return nullptr;
}

// Topological order of the before/after constraints; among the systems that are ready at the
// same time the registration order holds.
void WorldSystemRegistry::sort() {
    const size_t count = m_systems.size();
    std::vector<std::vector<size_t>> successors(count);
    std::vector<size_t> indegree(count, 0);
    const auto indexOf = [&](const std::string &name) -> int {
        for (size_t i = 0; i < count; i++) {
            if (m_systems[i].name == name) { return static_cast<int>(i); }
        }
        return -1;
    };
    const auto addEdge = [&](size_t from, size_t to) {
        if (from == to || std::find(successors[from].begin(), successors[from].end(), to) != successors[from].end()) {
            return;
        }
        successors[from].push_back(to);
        indegree[to]++;
    };
    for (size_t i = 0; i < count; i++) {
        const WorldSystem &system = m_systems[i];
        for (const std::string &name : system.after) {
            if (name == "*") {
                for (size_t j = 0; j < count; j++) {
                    const WorldSystem &other = m_systems[j];
                    if (j == i || contains(system.before, other.name) || contains(other.after, "*")
                        || contains(other.after, system.name)) {
                        continue;
                    }
                    addEdge(j, i);
                }
            } else if (const int j = indexOf(name); j >= 0) {
                addEdge(static_cast<size_t>(j), i);
            }
        }
        for (const std::string &name : system.before) {
            if (name == "*") {
                for (size_t j = 0; j < count; j++) {
                    const WorldSystem &other = m_systems[j];
                    if (j == i || contains(system.after, other.name) || contains(other.before, "*")
                        || contains(other.before, system.name)) {
                        continue;
                    }
                    addEdge(i, j);
                }
            } else if (const int j = indexOf(name); j >= 0) {
                addEdge(i, static_cast<size_t>(j));
            }
        }
    }
    std::vector<bool> done(count, false);
    m_ordered.clear();
    m_ordered.reserve(count);
    while (m_ordered.size() < count) {
        bool progressed = false;
        for (size_t i = 0; i < count; i++) {
            if (done[i] || indegree[i] != 0) { continue; }
            done[i] = true;
            m_ordered.push_back(&m_systems[i]);
            for (size_t successor : successors[i]) { indegree[successor]--; }
            progressed = true;
            break; // back to the lowest index: the registration order among the ready ones
        }
        if (!progressed) {
            LOG_ERROR("WORLD SYSTEMS: THE ORDER CONSTRAINTS FORM A CYCLE; THE REGISTRATION ORDER IS KEPT FOR THE REST");
            for (size_t i = 0; i < count; i++) {
                if (!done[i]) {
                    done[i] = true;
                    m_ordered.push_back(&m_systems[i]);
                }
            }
        }
    }
}

} // namespace Panda
