#include "Project/Documents/WorldFile.hpp"
#include "Project/Documents/FileDocuments.hpp"

#include <Panda/GameLogic/GameContext.hpp>
#include <Fern/Files/AtomicWrite.hpp>
#include <Panda/Prefabs/Prefabs.hpp>

#include <fstream>
#include <sstream>

namespace Panda {

WorldFile::WorldFile(FileDocuments &owner, uint32_t id)
    : m_owner(owner)
    , m_id(id) {}

std::string WorldFile::name() const {
    return m_path.empty() ? "Untitled World" : m_path.filename().string();
}

std::string WorldFile::title() const {
    return m_path.empty() ? "the new world" : titleOf(m_path);
}

std::string WorldFile::titleOf(const path_t &path) {
    if (path.empty()) { return "the new world"; }
    return (isPrefabFile(path) ? "prefab \"" : "world \"") + path.stem().string() + "\"";
}

Entity WorldFile::prefabRoot() {
    if (!isPrefab()) { return {}; }
    Entity root = m_world.getById(m_prefabRoot);
    if (root.isValid() && !root.getParent().isValid()) { return root; }
    const std::vector<UUID> roots = m_world.orderedChildren(UUID(0));
    m_prefabRoot = roots.empty() ? UUID(0) : roots.front();
    return m_world.getById(m_prefabRoot);
}

// What follows a write or a read for the project (the start world, the asset registry, the panels
// of the active world) is the collection's.
bool WorldFile::save() {
    return m_owner.saveWorld(*this);
}

bool WorldFile::reload() {
    return m_owner.reloadWorld(*this);
}

namespace {

    bool readText(const path_t &path, std::string &text) {
        std::ifstream file(path);
        if (!file.is_open()) { return false; }
        std::stringstream content;
        content << file.rdbuf();
        text = content.str();
        return true;
    }

} // namespace

bool WorldFile::read(const path_t &path) {
    std::string text;
    if (!readText(path, text)) {
        LOG_ERROR_EDITOR("World file not found: %s", path.string().c_str());
        return false;
    }
    if (!load(text, path, path)) { return false; }
    m_stamp.sync(path);
    m_world.setChanged(false);
    LOG_TRACE("Loaded world %s", path.string().c_str());
    return true;
}

bool WorldFile::restore(const path_t &copy, const path_t &path, int64_t stamp) {
    std::string text;
    if (!readText(copy, text)) {
        LOG_ERROR_EDITOR("The backup %s is not there.", copy.string().c_str());
        return false;
    }
    if (!load(text, path, copy)) { return false; }
    m_stamp.setTicks(stamp);
    m_world.setChanged(); // edits the file does not have: Save writes them, Don't Save drops them
    return true;
}

bool WorldFile::load(const std::string &text, const path_t &path, const path_t &source) {
    // Broken JSON: the world in memory and its path are NOT touched — fix the file and repeat
    // Reload; an accidental Save does not silently overwrite the broken file. The instances of
    // prefabs the file holds become entities here (context/prefabs.md).
    if (!Prefabs::load(m_world, text)) {
        LOG_ERROR_EDITOR("The file %s is not valid JSON and was not loaded.", source.string().c_str());
        return false;
    }
    m_path = path;
    m_prefabRoot = 0;
    if (isPrefab()) {
        const size_t roots = m_world.orderedChildren(UUID(0)).size();
        if (roots == 0) {
            // A file without an entity is still a prefab to edit: it gets its root.
            Entity root = m_world.instantiateEntity();
            root.setName(m_path.stem().string());
        } else if (roots > 1) {
            LOG_WARN_EDITOR(
                "%s has %zu entities without a parent. A prefab is one tree: \"%s\" is its root, and the others are left "
                "out when it is saved.",
                m_path.filename().string().c_str(), roots, prefabRoot().getName().c_str()
            );
        }
        prefabRoot();
        // The prefab is shown alone, under the built-in sky: the light of the sky reaches it, and
        // it stands before something. The sky is no part of the file — a prefab is written from
        // its root down — and the Hierarchy shows the tree of the root alone.
        Entity sky = m_world.instantiateEntity();
        sky.setName("Stage Sky");
        sky.addComponent<SkyComponent>();
    }
    // The script containers follow the loaded script build (classes and fields by its manifest).
    ScriptEngine *scripts = GameContext::getScriptEngine();
    if (scripts && scripts->isLoaded()) { m_world.updateScriptsAndFields(); }
    return true;
}

bool WorldFile::text(std::string &text) {
    if (isPrefab()) {
        Entity root = prefabRoot();
        if (!root.isValid()) { return false; }
        text = Prefabs::save(m_world, root);
    } else {
        text = Prefabs::save(m_world);
    }
    return true;
}

bool WorldFile::write(const path_t &path) {
    // The text first: a prefab without its root has nothing to write, and the file stays.
    std::string text;
    if (isPrefabFile(path)) {
        Entity root = prefabRoot();
        if (!root.isValid()) {
            LOG_ERROR_EDITOR("%s has no root entity and was not saved.", path.filename().string().c_str());
            return false;
        }
        text = Prefabs::save(m_world, root);
    } else {
        text = Prefabs::save(m_world);
    }
    std::string error;
    if (!Fern::writeFileAtomically(path, text, &error)) {
        LOG_ERROR_EDITOR("Not saved: %s.", error.c_str());
        return false;
    }
    m_world.setChanged(false);
    m_path = path;
    m_stamp.sync(path);
    return true;
}

} // namespace Panda
