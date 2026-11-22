#include "Project/Documents/DocumentBackups.hpp"
#include "Project/System/JsonFiles.hpp"
#include "Project/System/SystemTools.hpp"

#include <Fern/Files/AtomicWrite.hpp>
#include <Foundation/Logger.hpp>
#include <Rain/Rain.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <system_error>

namespace Panda {

namespace {

    constexpr const char *LIST_FILE = "backups.json";
    constexpr const char *LOCK_FILE = "lock";

    // One document of the list of a session folder.
    struct BackupEntry final : public Rain::Codable {
        std::string file;  // the file of the document (utf8Path); empty — a world that has none
        std::string copy;  // the name of its copy in the folder
        int64_t stamp = 0; // DiskStamp::ticks of the file the edits were made over

        RAIN_FIELDS_BEGIN(BackupEntry)
        RAIN_FIELD(file)
        RAIN_FIELD(copy)
        RAIN_FIELD(stamp)
        RAIN_FIELDS_END
    };

    struct BackupList final : public Rain::Codable {
        std::string project; // for a person who looks into the folder
        std::vector<BackupEntry> documents;

        RAIN_FIELDS_BEGIN(BackupList)
        RAIN_FIELD(project)
        RAIN_FIELD(documents)
        RAIN_FIELDS_END
    };

    // FNV-1a: the same path gives the same folder in every session.
    uint64_t hashText(const std::string &text) {
        uint64_t hash = 14695981039346656037ull;
        for (const char c : text) {
            hash ^= static_cast<unsigned char>(c);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    path_t projectFolderOf(const path_t &projectPath) {
        const path_t data = SystemTools::userDataDirectory();
        if (data.empty() || projectPath.empty()) { return {}; }
        std::error_code error;
        path_t canonical = std::filesystem::weakly_canonical(projectPath, error);
        if (error) { canonical = projectPath; }
        char hash[17];
        std::snprintf(hash, sizeof(hash), "%016llx", static_cast<unsigned long long>(hashText(utf8Path(canonical))));
        return data / "Backups" / pathFromUtf8(utf8Path(canonical.filename()) + "-" + hash);
    }

} // namespace

DocumentBackups::DocumentBackups() = default;

DocumentBackups::~DocumentBackups() = default;

void DocumentBackups::open(const path_t &projectPath) {
    close();
    m_projectPath = projectPath;
    m_projectFolder = projectFolderOf(projectPath);
    m_failed = false;
    std::error_code error;
    if (m_projectFolder.empty() || !std::filesystem::is_directory(m_projectFolder, error)) { return; }
    // A folder whose lock is free was left behind: its session ended without removing it. Its lock
    // stays taken here until it is answered, so that another editor does not offer it as well.
    for (const auto &entry : std::filesystem::directory_iterator(m_projectFolder, error)) {
        if (!entry.is_directory(error)) { continue; }
        auto lock = std::make_unique<Fern::FileLock>();
        if (!lock->tryLock(entry.path() / LOCK_FILE)) { continue; } // a live session
        BackupList list;
        std::vector<LeftBackup> found;
        if (readJsonFile(entry.path() / LIST_FILE, list)) {
            for (const BackupEntry &document : list.documents) {
                const path_t copy = entry.path() / pathFromUtf8(document.copy);
                if (document.copy.empty() || !std::filesystem::is_regular_file(copy, error)) { continue; }
                found.push_back({document.file.empty() ? path_t() : pathFromUtf8(document.file), copy, document.stamp});
            }
        }
        if (found.empty()) { // nothing to give back: a session that had no unsaved edits
            lock->unlock();
            std::filesystem::remove_all(entry.path(), error);
            continue;
        }
        m_leftBehind.insert(m_leftBehind.end(), found.begin(), found.end());
        m_leftFolders.push_back(entry.path());
        m_leftLocks.push_back(std::move(lock));
    }
}

void DocumentBackups::close() {
    std::error_code error;
    if (!m_folder.empty()) {
        m_lock.unlock();
        std::filesystem::remove_all(m_folder, error);
    }
    m_folder.clear();
    m_copies.clear();
    m_leftBehind.clear();
    m_leftFolders.clear();
    m_leftLocks.clear();
    // The folder of the project goes with its last session.
    if (!m_projectFolder.empty() && std::filesystem::is_empty(m_projectFolder, error)) {
        std::filesystem::remove(m_projectFolder, error);
    }
    m_projectFolder.clear();
    m_projectPath.clear();
}

bool DocumentBackups::isCurrent(uint32_t key, uint64_t revision, const path_t &file, int64_t stamp) const {
    const auto found = m_copies.find(key);
    return found != m_copies.end() && found->second.revision == revision && found->second.file == file &&
           found->second.stamp == stamp;
}

bool DocumentBackups::makeFolder() {
    if (!m_folder.empty()) { return true; }
    if (m_projectFolder.empty()) { return false; }
    // A name no other session has: the moment it began.
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const path_t folder =
        m_projectFolder / ("session-" + std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(now).count()));
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    if (error || !m_lock.tryLock(folder / LOCK_FILE)) {
        if (!m_failed) {
            LOG_WARN_EDITOR(
                "The unsaved edits are not backed up: %s cannot be made (%s).", utf8Path(folder).c_str(),
                error ? error.message().c_str() : "it is locked"
            );
        }
        m_failed = true;
        return false;
    }
    m_folder = folder;
    return true;
}

bool DocumentBackups::writeList() {
    BackupList list;
    list.project = utf8Path(m_projectPath);
    for (const auto &[key, copy] : m_copies) {
        BackupEntry entry;
        entry.file = copy.file.empty() ? std::string() : utf8Path(copy.file);
        entry.copy = copy.name;
        entry.stamp = copy.stamp;
        list.documents.push_back(std::move(entry));
    }
    std::string error;
    if (!writeJsonFile(m_folder / LIST_FILE, list, &error)) {
        if (!m_failed) { LOG_WARN_EDITOR("The unsaved edits are not backed up: %s.", error.c_str()); }
        m_failed = true;
        return false;
    }
    return true;
}

bool DocumentBackups::write(uint32_t key, uint64_t revision, const path_t &file, int64_t stamp, const std::string &text) {
    if (!makeFolder()) { return false; }
    Copy copy;
    copy.file = file;
    copy.stamp = stamp;
    copy.revision = revision;
    copy.name = std::to_string(key) + (file.extension() == ".prefab" ? ".prefab" : ".world");
    // The text first, then the list that names it: a list never names a copy that is not there.
    const auto previous = m_copies.find(key);
    const bool listed = previous != m_copies.end() && previous->second.file == file &&
                        previous->second.stamp == stamp && previous->second.name == copy.name;
    if (previous == m_copies.end() || previous->second.revision != revision) {
        std::string error;
        if (!Fern::writeFileAtomically(m_folder / copy.name, text, &error)) {
            if (!m_failed) { LOG_WARN_EDITOR("The unsaved edits are not backed up: %s.", error.c_str()); }
            m_failed = true;
            return false;
        }
    }
    m_copies[key] = copy;
    if (!listed && !writeList()) { return false; }
    m_failed = false;
    return true;
}

void DocumentBackups::remove(uint32_t key) {
    const auto found = m_copies.find(key);
    if (found == m_copies.end()) { return; }
    const path_t copy = m_folder / found->second.name;
    m_copies.erase(found);
    // The list first: it never names a copy that is gone.
    writeList();
    std::error_code error;
    std::filesystem::remove(copy, error);
}

std::vector<uint32_t> DocumentBackups::keys() const {
    std::vector<uint32_t> keys;
    keys.reserve(m_copies.size());
    for (const auto &[key, copy] : m_copies) { keys.push_back(key); }
    return keys;
}

void DocumentBackups::dropLeftBehind() {
    std::error_code error;
    for (size_t i = 0; i < m_leftFolders.size(); i++) {
        m_leftLocks[i]->unlock();
        std::filesystem::remove_all(m_leftFolders[i], error);
    }
    m_leftBehind.clear();
    m_leftFolders.clear();
    m_leftLocks.clear();
}

} // namespace Panda
