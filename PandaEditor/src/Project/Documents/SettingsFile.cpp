#include "Project/Documents/SettingsFile.hpp"
#include "Project/Documents/FileDocuments.hpp"

#include <Panda/Serialization/SettingsMapper.hpp>
#include <Rain/Coders/JsonDecoder.hpp>
#include <Rain/Coders/JsonEncoder.hpp>

#include <fstream>

namespace Panda {

SettingsFile::SettingsFile(FileDocuments &owner, SettingsScope scope, std::string title)
    : m_owner(owner)
    , m_scope(scope)
    , m_title(std::move(title)) {}

void SettingsFile::open(const path_t &path) {
    m_path = path;
    m_dirty = false;
    SettingsMapper::reset(m_scope, m_settings);
    if (m_path.empty()) { return; }
    std::ifstream file(m_path);
    if (!file.is_open()) {
        LOG_INFO("SETTINGS FILE NOT FOUND, THE DEFAULTS ARE USED: %s", m_path.string().c_str());
    } else {
        Rain::JsonDecoder decoder;
        if (!SettingsMapper::load(m_scope, m_settings, decoder, file)) {
            LOG_ERROR_EDITOR("SETTINGS FILE IS BROKEN: %s", m_path.string().c_str());
        }
    }
    m_stamp.sync(m_path);
}

bool SettingsFile::write() {
    if (m_path.empty()) { return false; }
    std::error_code errorCode;
    std::filesystem::create_directories(m_path.parent_path(), errorCode); // .Panda of a new project
    std::ofstream file(m_path);
    if (!file.is_open()) {
        LOG_ERROR_EDITOR("Cannot write the settings file: %s", m_path.string().c_str());
        return false;
    }
    Rain::JsonEncoder encoder(true);
    SettingsMapper::save(m_scope, m_settings, encoder, file);
    file.close();
    m_stamp.sync(m_path);
    m_dirty = false;
    return true;
}

bool SettingsFile::save() {
    if (!write()) { return false; }
    m_owner.settingsWritten(*this);
    return true;
}

void SettingsFile::saveUnasked() {
    if (m_stamp.isStale(m_path)) {
        LOG_WARN_EDITOR("%s: the file was changed on disk, the editor's copy is not written", m_path.filename().string().c_str());
        return;
    }
    write();
}

bool SettingsFile::reload() {
    if (m_path.empty()) { return false; }
    // A broken file leaves the scope as it is: decoded into a scratch store first. A file that is
    // gone is the defaults.
    TypeContext scratch;
    SettingsMapper::reset(m_scope, scratch);
    std::ifstream file(m_path);
    if (file.is_open()) {
        Rain::JsonDecoder decoder;
        if (!SettingsMapper::load(m_scope, scratch, decoder, file)) {
            LOG_ERROR_EDITOR("SETTINGS FILE IS BROKEN: %s", m_path.string().c_str());
            return false;
        }
    }
    SettingsMapper::copy(m_scope, scratch, m_settings);
    m_stamp.sync(m_path);
    m_dirty = false;
    m_owner.settingsRead(*this);
    return true;
}

} // namespace Panda
