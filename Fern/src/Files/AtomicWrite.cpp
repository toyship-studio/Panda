// Fern::writeFileAtomically (Fern/Files/AtomicWrite.hpp): a new file next to the old one, flushed,
// then renamed over it. POSIX for macOS and Linux, Win32 for Windows.

#include "Fern/Files/AtomicWrite.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <system_error>
#include <thread>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#else
#    include <cerrno>
#    include <fcntl.h>
#    include <sys/stat.h>
#    include <unistd.h>
#endif

namespace Fern {

namespace {

    namespace fs = std::filesystem;

    // A path in words for a message: UTF-8 on every system (path::string() on Windows is in the
    // code page of the system and refuses what it cannot hold).
    std::string text(const fs::path &path) {
        const std::u8string utf8 = path.u8string();
        return std::string(utf8.begin(), utf8.end());
    }

#ifdef _WIN32

    std::string why(DWORD code) {
        return std::system_category().message(static_cast<int>(code));
    }

    unsigned long processId() {
        return GetCurrentProcessId();
    }

    // A file that is not there yet (one that is, is not touched — `taken`) with the bytes in it,
    // flushed to the drive. Windows keeps no permissions to carry over: a read-only file is
    // refused before (writeFileAtomically).
    bool writeNew(const fs::path &path, std::string_view bytes, const fs::perms *, bool &taken, std::string &error) {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            const DWORD code = GetLastError();
            taken = code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS;
            error = why(code);
            return false;
        }
        DWORD code = 0;
        size_t written = 0;
        while (code == 0 && written < bytes.size()) {
            const DWORD part = static_cast<DWORD>(std::min<size_t>(bytes.size() - written, size_t(1) << 30));
            DWORD done = 0;
            if (!WriteFile(file, bytes.data() + written, part, &done, nullptr)) {
                code = GetLastError();
            } else if (done == 0) {
                code = ERROR_WRITE_FAULT;
            }
            written += done;
        }
        if (code == 0 && !FlushFileBuffers(file)) { code = GetLastError(); }
        if (!CloseHandle(file) && code == 0) { code = GetLastError(); }
        if (code != 0) {
            error = why(code);
            return false;
        }
        return true;
    }

    // The new file takes the place of the old one. A scanner of the system (an antivirus, the
    // indexer) holds a file that was just written for a moment: the move is tried again for a
    // while before it is given up.
    bool replace(const fs::path &from, const fs::path &to, std::string &error) {
        DWORD code = 0;
        for (int attempt = 0; attempt < 20; attempt++) {
            if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { return true; }
            code = GetLastError();
            if (code != ERROR_ACCESS_DENIED && code != ERROR_SHARING_VIOLATION && code != ERROR_LOCK_VIOLATION) { break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        error = why(code);
        return false;
    }

#else

    std::string why(int code) {
        return std::generic_category().message(code);
    }

    unsigned long processId() {
        return static_cast<unsigned long>(::getpid());
    }

    // A file that is not there yet (one that is, is not touched — `taken`) with the bytes in it,
    // flushed to the drive. fsync, not F_FULLFSYNC on macOS: that one also empties the cache of
    // the drive and costs 4–5 ms a file there — 2.5 s for the sidecars of 500 new assets.
    bool writeNew(const fs::path &path, std::string_view bytes, const fs::perms *keep, bool &taken, std::string &error) {
        const int file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
        if (file < 0) {
            taken = errno == EEXIST;
            error = why(errno);
            return false;
        }
        int code = 0;
        // The permissions of the file it replaces, as they are: open() takes the umask off.
        if (keep != nullptr && ::fchmod(file, static_cast<mode_t>(*keep & fs::perms::mask)) != 0) { code = errno; }
        size_t written = 0;
        while (code == 0 && written < bytes.size()) {
            const ssize_t done = ::write(file, bytes.data() + written, bytes.size() - written);
            if (done < 0) {
                if (errno != EINTR) { code = errno; }
            } else if (done == 0) {
                code = EIO;
            } else {
                written += static_cast<size_t>(done);
            }
        }
        if (code == 0 && ::fsync(file) != 0) { code = errno; }
        if (::close(file) != 0 && code == 0) { code = errno; }
        if (code != 0) {
            error = why(code);
            return false;
        }
        return true;
    }

    bool replace(const fs::path &from, const fs::path &to, std::string &error) {
        if (::rename(from.c_str(), to.c_str()) == 0) { return true; }
        error = why(errno);
        return false;
    }

#endif

} // namespace

bool writeFileAtomically(const fs::path &path, std::string_view bytes, std::string *error) {
    const auto fail = [error](std::string message) {
        if (error != nullptr) { *error = std::move(message); }
        return false;
    };
    std::error_code code;
    fs::path target = path;
    // A link stays a link: what it points to is the file that is written. A link to nothing is
    // written over.
    if (fs::is_symlink(fs::symlink_status(target, code))) {
        fs::path resolved = fs::canonical(target, code);
        if (!code) { target = std::move(resolved); }
    }
    const fs::file_status status = fs::status(target, code);
    const bool exists = fs::exists(status);
    if (exists && !fs::is_regular_file(status)) { return fail(text(target) + " is not a file"); }
    if (exists && (status.permissions() & fs::perms::owner_write) == fs::perms::none) {
        return fail(text(target) + " is read-only");
    }
    const fs::perms permissions = status.permissions();

    static std::atomic<unsigned> counter{0};
    std::string reason;
    fs::path temporary;
    for (int attempt = 0;; attempt++) {
        fs::path name = ".";
        name += target.filename();
        name += "." + std::to_string(processId()) + "-" + std::to_string(counter++) + "~";
        temporary = target.parent_path() / name;
        bool taken = false;
        if (writeNew(temporary, bytes, exists ? &permissions : nullptr, taken, reason)) { break; }
        if (taken && attempt < 100) { continue; } // left by a process that had the same id
        if (!taken) { fs::remove(temporary, code); } // a full disk stops a write in the middle
        return fail("cannot write " + text(target) + ": " + reason);
    }
    if (!replace(temporary, target, reason)) {
        fs::remove(temporary, code);
        return fail("cannot replace " + text(target) + ": " + reason);
    }
    return true;
}

} // namespace Fern
