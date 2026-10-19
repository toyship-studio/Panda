#pragma once

#include <Panda/GameLogic/Entity.hpp>
#include <Panda/GameLogic/World.hpp>
#include <Panda/WorldCommands/CommandHistory.hpp>
#include <Panda/WorldCommands/Impl/AddRemoveComponentCommand.hpp>
#include <Panda/WorldCommands/WorldCommand.hpp>

#include <typeinfo>
#include <unordered_map>
#include <unordered_set>

namespace Editor {

// Component edits from inspector forms — a single path for ALL types: core and module ones.

// Add (add=true) / remove component T on entities. AddRemoveComponentCommand<T> is itself a
// toggle — the ones that need to be touched are filtered (add → those that do NOT have T; remove →
// those that DO).
template<typename T>
void toggleComponentPresence(Panda::World &world, const std::unordered_set<Panda::Entity> &entities, bool add) {
    std::unordered_set<Panda::Entity> targets;
    for (Panda::Entity entity : entities) {
        const bool has = entity.hasComponent<T>();
        if (add ? !has : has) { targets.insert(entity); }
    }
    if (targets.empty()) { return; }
    Panda::AddRemoveComponentCommand<T> command(targets);
    world.getCommandHistory().SAVE(command, true);
    world.setChanged();
}

// Generic edit command for component T: copies the component before/after, undo/redo through
// assignment, merging of consecutive edits of the same set (a drag = one undo).
template<typename T>
class GenericComponentUpdate : public Panda::WorldCommand {
public:
    explicit GenericComponentUpdate(const std::unordered_set<Panda::Entity> &entities)
        : m_entities(entities) {}
    bool execute() override {
        return restore(m_after);
    }
    bool undo() override {
        return restore(m_before);
    }
    bool canMerge(Panda::WorldCommand &other) override {
        return typeid(other) == typeid(*this) && static_cast<GenericComponentUpdate &>(other).m_entities == m_entities;
    }
    void merge(Panda::WorldCommand &other) override {
        m_after = static_cast<GenericComponentUpdate &>(other).m_after;
    }
    void saveBeforeEdit() {
        capture(m_before);
    }
    void saveAfterEdit() {
        capture(m_after);
    }

private:
    void capture(std::unordered_map<Panda::UUID, T> &dst) {
        for (Panda::Entity entity : m_entities) {
            if (entity.hasComponent<T>()) { dst[entity.getId()] = entity.getComponent<T>(); }
        }
    }
    bool restore(std::unordered_map<Panda::UUID, T> &src) {
        for (Panda::Entity entity : m_entities) {
            if (!entity.isValid()) { return false; }
            auto it = src.find(entity.getId());
            if (it != src.end() && entity.hasComponent<T>()) {
                entity.getComponent<T>() = it->second;
                entity.setWorldChanged();
            }
        }
        return true;
    }
    std::unordered_set<Panda::Entity> m_entities;
    std::unordered_map<Panda::UUID, T> m_before, m_after;
};

// Write the value of component T to all targets as one undoable edit.
template<typename T>
void commitComponent(Panda::World &world, const std::unordered_set<Panda::Entity> &targets, const T &value) {
    if (targets.empty()) { return; }
    GenericComponentUpdate<T> command(targets);
    command.saveBeforeEdit();
    for (Panda::Entity entity : targets) {
        if (entity.hasComponent<T>()) {
            entity.getComponent<T>() = value;
            entity.setWorldChanged();
        }
    }
    command.saveAfterEdit();
    world.getCommandHistory().SAVE(command, false);
}

// Same for the form's target list (ComponentInspectorContext::entities): written to those that have T.
template<typename T>
void commitComponent(Panda::World &world, const std::vector<Panda::Entity> &entities, const T &value) {
    std::unordered_set<Panda::Entity> targets;
    for (Panda::Entity entity : entities) {
        if (entity.hasComponent<T>()) { targets.insert(entity); }
    }
    commitComponent<T>(world, targets, value);
}

} // namespace Editor
