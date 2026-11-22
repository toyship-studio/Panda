#include "Fern/Files/FileLock.hpp"

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#else
#    include <fcntl.h>
#    include <sys/file.h>
#    include <unistd.h>
#endif

#include <utility>

namespace Fern {

FileLock::~FileLock() {
    unlock();
}

#ifdef _WIN32

FileLock::FileLock(FileLock &&other) noexcept
    : m_handle(std::exchange(other.m_handle, nullptr)) {}

FileLock &FileLock::operator=(FileLock &&other) noexcept {
    if (this != &other) {
        unlock();
        m_handle = std::exchange(other.m_handle, nullptr);
    }
    return *this;
}

bool FileLock::tryLock(const std::filesystem::path &path) {
    unlock();
    // Shared for reading, writing and removal: the lock is the byte range below, not the open
    // file — the folder of the file may go while another process looks into it. Not inherited
    // (no security attributes): a process the editor starts does not keep it alive.
    HANDLE file = CreateFileW(
        path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (file == INVALID_HANDLE_VALUE) { return false; }
    OVERLAPPED overlapped = {};
    if (!LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &overlapped)) {
        CloseHandle(file);
        return false;
    }
    m_handle = file;
    return true;
}

void FileLock::unlock() {
    if (m_handle == nullptr) { return; }
    OVERLAPPED overlapped = {};
    UnlockFileEx(static_cast<HANDLE>(m_handle), 0, 1, 0, &overlapped);
    CloseHandle(static_cast<HANDLE>(m_handle));
    m_handle = nullptr;
}

bool FileLock::isLocked() const {
    return m_handle != nullptr;
}

#else

FileLock::FileLock(FileLock &&other) noexcept
    : m_descriptor(std::exchange(other.m_descriptor, -1)) {}

FileLock &FileLock::operator=(FileLock &&other) noexcept {
    if (this != &other) {
        unlock();
        m_descriptor = std::exchange(other.m_descriptor, -1);
    }
    return *this;
}

bool FileLock::tryLock(const std::filesystem::path &path) {
    unlock();
    // O_CLOEXEC: a process the editor starts (a build, the game) does not keep the lock alive.
    // flock belongs to the open file: another open of the same file — in this process too — is
    // refused while this one holds it.
    const int descriptor = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (descriptor < 0) { return false; }
    if (::flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        ::close(descriptor);
        return false;
    }
    m_descriptor = descriptor;
    return true;
}

void FileLock::unlock() {
    if (m_descriptor < 0) { return; }
    ::flock(m_descriptor, LOCK_UN);
    ::close(m_descriptor);
    m_descriptor = -1;
}

bool FileLock::isLocked() const {
    return m_descriptor >= 0;
}

#endif

} // namespace Fern
