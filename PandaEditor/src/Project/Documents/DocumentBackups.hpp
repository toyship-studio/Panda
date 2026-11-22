#pragma once

#include <Fern/Files/FileLock.hpp>
#include <Panda/Assets/Base/Path.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Panda {

// The unsaved text of one document that a session of the project left behind.
struct LeftBackup final {
    path_t file;       // the file of the document; empty — a world that had none
    path_t copy;       // its text as it was
    int64_t stamp = 0; // the time of the file the edits were made over (DiskStamp::ticks)
};

// The copies of the unsaved edits, so that a crash — of the editor, or of the game code that runs
// in it — does not take them along (FileDocuments::backUp; context/architecture.md, "Backups").
// They are no files of the project: each session of a project writes into a folder of its own in
// the user data directory, `Backups/<project>-<hash of its path>/<session>/` — the text of each
// document (`<key>.world`, `<key>.prefab`) and the list of them (`backups.json`) — and holds the
// lock of that folder (Fern::FileLock) while it lives. An orderly end of the session removes the
// folder; a folder whose lock nobody holds was left behind by a session that did not end so, and
// its copies are offered back when the project opens. Another editor that has the project open
// at the same time keeps its own folder, locked: it is never taken for one left behind.
class DocumentBackups final {
public:
    DocumentBackups();
    ~DocumentBackups();

    // The project opens: the folders its sessions left behind are found and kept (locked) until
    // they are restored or discarded. The folder of this session is made with its first copy.
    void open(const path_t &projectPath);
    // The session of the project ends in order: its folder goes. What was left behind and not
    // answered stays for the next time the project opens.
    void close();

    // The copy of the document `key` holds what it would be written as at `revision` (of its
    // world), with `file` and `stamp` as the document has them.
    bool isCurrent(uint32_t key, uint64_t revision, const path_t &file, int64_t stamp) const;
    // The text of the document `key` (the extension of the copy — ".world", ".prefab" — is for a
    // person who looks into the folder). false — nothing was written; the log says why, once.
    bool write(uint32_t key, uint64_t revision, const path_t &file, int64_t stamp, const std::string &text);
    void remove(uint32_t key);
    std::vector<uint32_t> keys() const;

    const std::vector<LeftBackup> &leftBehind() const {
        return m_leftBehind;
    }
    // The folders left behind go, with their copies (restored into documents, or not wanted).
    void dropLeftBehind();

private:
    struct Copy {
        path_t file;
        int64_t stamp = 0;
        uint64_t revision = 0;
        std::string name; // of the copy in the folder
    };
    bool makeFolder();
    bool writeList();

    path_t m_projectPath;
    path_t m_projectFolder; // Backups/<project>-<hash>
    path_t m_folder;        // the folder of this session; empty — not made yet
    Fern::FileLock m_lock;  // of m_folder
    bool m_failed = false;  // a write failed and was logged: the next ones are not
    std::unordered_map<uint32_t, Copy> m_copies;
    std::vector<LeftBackup> m_leftBehind;
    std::vector<path_t> m_leftFolders;
    std::vector<std::unique_ptr<Fern::FileLock>> m_leftLocks;
};

} // namespace Panda
