#pragma once

#include "Fern/Files/FileWatcher.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

namespace Fern::Files {

// Where the backends report: the changes wait here for the owner of the watcher. It knows what
// is watched — the folders and the single files, as real paths — and keeps only what lies there.
class ChangeSink final {
public:
    explicit ChangeSink(const std::vector<std::filesystem::path> &paths);

    const std::vector<std::filesystem::path> &folders() const {
        return m_folders;
    }
    const std::vector<std::filesystem::path> &files() const {
        return m_files;
    }

    // From any thread.
    void changed(const std::filesystem::path &path);
    void lostTrack();
    bool take(FileWatcher::Changes &changes);

private:
    bool watches(const std::filesystem::path &path) const;

    std::vector<std::filesystem::path> m_folders, m_files;
    std::mutex m_mutex;
    std::set<std::filesystem::path> m_pending;
    bool m_everything = false;
};

// A running watch; it stops when it is destroyed, and reports nothing after that.
class Backend {
public:
    virtual ~Backend() = default;
};

// The backend of the platform over the watched folders (sink->folders(), each with everything
// under it); nullptr — the platform has none, or it would not start.
std::unique_ptr<Backend> startSystemBackend(std::shared_ptr<ChangeSink> sink);
// The portable one: the given folders (with everything under them) and files are read again and
// again and compared with what they held.
std::unique_ptr<Backend> startScanBackend(
    std::shared_ptr<ChangeSink> sink, std::vector<std::filesystem::path> folders, std::vector<std::filesystem::path> files
);

// `path` lies under `folder` (both real paths: the comparison is of their text).
bool isUnder(const std::filesystem::path &path, const std::filesystem::path &folder);

} // namespace Fern::Files
