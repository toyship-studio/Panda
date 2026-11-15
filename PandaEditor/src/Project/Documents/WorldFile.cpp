#include "Project/Documents/WorldFile.hpp"
#include "Project/Documents/FileDocuments.hpp"

#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/Serialization/WorldMapper.hpp>
#include <Rain/Coders/JsonDecoder.hpp>
#include <Rain/Coders/JsonEncoder.hpp>

#include <fstream>

namespace Panda {

WorldFile::WorldFile(FileDocuments &owner, uint32_t id)
    : m_owner(owner)
    , m_id(id) {}

std::string WorldFile::name() const {
    return m_path.empty() ? "Untitled World" : m_path.filename().string();
}

std::string WorldFile::title() const {
    return m_path.empty() ? "the new world" : "world \"" + m_path.stem().string() + "\"";
}

// What follows a write or a read for the project (the start world, the asset registry, the panels
// of the active world) is the collection's.
bool WorldFile::save() {
    return m_owner.saveWorld(*this);
}

bool WorldFile::reload() {
    return m_owner.reloadWorld(*this);
}

bool WorldFile::read(const path_t &path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR_EDITOR("World file not found: %s", path.string().c_str());
        return false;
    }
    Rain::JsonDecoder decoder;
    // Broken JSON: the world in memory and its path are NOT touched — fix the file and repeat
    // Reload; an accidental Save does not silently overwrite the broken file.
    if (!WorldMapper::load(m_world, decoder, file)) {
        LOG_ERROR_EDITOR("The world file %s is not valid JSON and was not loaded.", path.string().c_str());
        return false;
    }
    m_path = path;
    m_stamp.sync(path);
    // The script containers follow the loaded script build (classes and fields by its manifest).
    ScriptEngine *scripts = GameContext::getScriptEngine();
    if (scripts && scripts->isLoaded()) { m_world.updateScriptsAndFields(); }
    LOG_TRACE("Loaded world %s", path.string().c_str());
    return true;
}

bool WorldFile::write(const path_t &path) {
    std::ofstream file(path);
    if (!file.is_open()) {
        LOG_ERROR_EDITOR("Cannot write the world file: %s", path.string().c_str());
        return false;
    }
    Rain::JsonEncoder encoder(true);
    WorldMapper::save(m_world, encoder, file);
    file.close();
    m_world.setChanged(false);
    m_path = path;
    m_stamp.sync(path);
    return true;
}

} // namespace Panda
