#pragma once

#include "Project/Documents/FileDocument.hpp"

#include <Panda/Base/TypeContext.hpp>
#include <Panda/Extensions/Settings.hpp>

#include <string>

namespace Panda {

class FileDocuments;

// A settings scope and its JSON file (the settings model, context/plugins.md): `settings.json`
// of the user is the editor scope, `.Panda/project.json` of the project is the project scope. The
// document owns the store — the sections of the scope by type, the core's and the modules' — and
// reads and writes it with SettingsMapper.
//
// The editor scope is written on every change (the caller saves after the edit), so its memory is
// never ahead of the file. The project scope collects edits until Save (markEdited).
class SettingsFile final : public FileDocument {
public:
    // The store is empty until open(): the sections of the scope are registered by then.
    SettingsFile(FileDocuments &owner, SettingsScope scope, std::string title);

    SettingsScope scope() const {
        return m_scope;
    }
    TypeContext &settings() {
        return m_settings;
    }

    // The file of the scope and its first read: a file that does not exist is the defaults, a
    // broken one keeps the defaults and says so. An empty path — the scope has no file (no user
    // data directory, no project) and lives in memory: the defaults.
    void open(const path_t &path);
    // An edit went into the store: the file is behind until Save.
    void markEdited() {
        m_dirty = true;
    }
    // A write nobody asked for (the exit, closing the project): a file changed on disk since the
    // editor read it is left alone — the file is the contract. Nothing follows a write like this.
    void saveUnasked();

    std::string title() const override {
        return m_title;
    }
    path_t path() const override {
        return m_path;
    }
    bool isDirty() const override {
        return m_dirty;
    }
    bool isChangedOnDisk() const override {
        return m_stamp.isStale(m_path);
    }
    bool save() override;
    bool reload() override; // a broken file leaves the store as it is

private:
    bool write();

    FileDocuments &m_owner;
    SettingsScope m_scope;
    std::string m_title;
    TypeContext m_settings;
    path_t m_path;
    DiskStamp m_stamp;
    bool m_dirty = false;
};

} // namespace Panda
