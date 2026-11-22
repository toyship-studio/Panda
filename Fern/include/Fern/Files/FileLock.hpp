#pragma once
// A lock of a file that the system lets go when the process ends in any way — a crash and a kill
// too: what tells a folder whose owner is alive from a folder a process left behind.

#include <filesystem>

namespace Fern {

// An exclusive lock of one file, held while the object holds it. The file is made when it is not
// there and stays when the lock goes (whoever removes the folder removes it). The lock is the
// system's — flock on macOS, iOS and Linux, LockFileEx on Windows — so another process that asks
// for it is refused until this one lets go or ends; a process started from this one does not
// inherit it. Advisory: it keeps out only those who ask for it.
class FileLock final {
public:
    FileLock() = default;
    ~FileLock();
    FileLock(const FileLock &) = delete;
    FileLock &operator=(const FileLock &) = delete;
    FileLock(FileLock &&other) noexcept;
    FileLock &operator=(FileLock &&other) noexcept;

    // Takes the lock without waiting; a lock held before is let go first. false — somebody holds
    // it (another process, or another FileLock of this one), or the file cannot be made.
    bool tryLock(const std::filesystem::path &path);
    void unlock();
    bool isLocked() const;

private:
#ifdef _WIN32
    void *m_handle = nullptr; // HANDLE
#else
    int m_descriptor = -1;
#endif
};

} // namespace Fern
