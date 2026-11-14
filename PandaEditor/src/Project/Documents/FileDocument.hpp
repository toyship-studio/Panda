#pragma once

#include <Panda/Assets/Base/Path.hpp>

#include <filesystem>
#include <string>
#include <system_error>

namespace Panda {

// A file the editor holds in memory and edits (the document model, context/architecture.md): a
// world, the settings of the project and of the editor, an asset. It is the unit of Save, of
// Reload and of the unsaved-changes questions — those three operations are written once over
// this interface (EditorLayer), whatever the file is. A tab is a view of one file document; a
// file document exists without a tab too (a loaded texture, the settings with their tab closed).
// Every kind lives in this folder, and FileDocuments is the one place that holds them.
class FileDocument {
public:
    virtual ~FileDocument() = default;

    virtual std::string title() const = 0; // how a question names it: `world "Forest"`, `"Bark"`
    virtual path_t path() const = 0;       // empty — no file yet (a new world)
    // The memory has edits the file does not.
    virtual bool isDirty() const = 0;
    // The file is newer than what the editor last read or wrote (an edit by hand, an agent, git).
    virtual bool isChangedOnDisk() const = 0;
    virtual bool save() = 0;   // memory → file; false — nothing was written (an error, a cancelled dialog)
    virtual bool reload() = 0; // file → memory, the edits in memory are dropped; false — the file is broken
};

// The time of a file as the editor last read or wrote it: what "changed on disk" is measured from.
class DiskStamp final {
public:
    // The editor has just read or written the file.
    void sync(const path_t &path) {
        std::error_code errorCode;
        const auto time = std::filesystem::last_write_time(path, errorCode);
        m_time = errorCode ? std::filesystem::file_time_type{} : time;
    }
    // The file is newer than the last sync; a file that does not exist is not.
    bool isStale(const path_t &path) const {
        if (path.empty()) { return false; }
        std::error_code errorCode;
        const auto time = std::filesystem::last_write_time(path, errorCode);
        return !errorCode && time > m_time;
    }

private:
    std::filesystem::file_time_type m_time{};
};

} // namespace Panda
