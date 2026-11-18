// The file watch of Windows: ReadDirectoryChangesW. Every watched folder is open with a read of
// its changes always pending — the folders under it are the system's to follow. The reads end
// on one completion port, where a thread of the backend waits for them and hands the names to
// the sink.
#include "Files/FileWatcherInternal.hpp"

#include <Foundation/Thread.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <string>
#include <thread>

namespace Fern::Files {

namespace {

    // How much a read brings at once: the most a read over a network share takes. Changes that
    // do not fit are not lost in silence — the read ends with nothing, and the sink is told
    // that the track is lost.
    constexpr DWORD BUFFER_BYTES = 64 * 1024;

    // What is asked about: the names made, removed and renamed, and the files written.
    constexpr DWORD WATCHED = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE |
                              FILE_NOTIFY_CHANGE_SIZE;

    bool isFolder(const std::filesystem::path &path) {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    class DirectoryChangesBackend final : public Backend {
    public:
        explicit DirectoryChangesBackend(std::shared_ptr<ChangeSink> sink)
            : m_sink(std::move(sink)) {}

        bool start() {
            m_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
            if (m_port == nullptr) { return false; }
            for (const std::filesystem::path &path : m_sink->folders()) {
                m_folders.push_back(std::make_unique<Folder>());
                Folder &folder = *m_folders.back();
                folder.path = path;
                folder.buffer.resize(BUFFER_BYTES / sizeof(DWORD));
                folder.handle = CreateFileW(
                    path.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr
                );
                if (folder.handle == INVALID_HANDLE_VALUE) { return false; }
                // A read that ended comes to the port with its folder as the key.
                if (CreateIoCompletionPort(folder.handle, m_port, reinterpret_cast<ULONG_PTR>(&folder), 0) == nullptr) {
                    return false;
                }
                if (!read(folder)) { return false; }
            }
            m_thread = std::thread([this] { run(); });
            return true;
        }

        ~DirectoryChangesBackend() override {
            if (m_thread.joinable()) {
                PostQueuedCompletionStatus(m_port, 0, 0, nullptr); // the word to stop: an end of no read
                m_thread.join();
            }
            // A read that is still pending is called off, and its end is awaited: until it
            // comes, its buffer is the system's to write.
            size_t pending = 0;
            for (const std::unique_ptr<Folder> &folder : m_folders) {
                if (!folder->reading) { continue; }
                CancelIoEx(folder->handle, &folder->overlapped);
                pending++;
            }
            while (pending > 0) {
                DWORD bytes = 0;
                ULONG_PTR key = 0;
                OVERLAPPED *overlapped = nullptr;
                const BOOL taken = GetQueuedCompletionStatus(m_port, &bytes, &key, &overlapped, INFINITE);
                if (overlapped != nullptr) {
                    pending--;
                } else if (!taken) {
                    break; // the port itself failed: nothing more comes from it
                }
            }
            for (const std::unique_ptr<Folder> &folder : m_folders) {
                if (folder->handle != INVALID_HANDLE_VALUE) { CloseHandle(folder->handle); }
            }
            if (m_port != nullptr) { CloseHandle(m_port); }
        }

    private:
        struct Folder {
            std::filesystem::path path;
            HANDLE handle = INVALID_HANDLE_VALUE;
            OVERLAPPED overlapped = {};
            std::vector<DWORD> buffer; // of DWORDs: the records of a read are aligned so
            bool reading = false;      // a read is pending: its end is yet to come to the port
        };

        bool read(Folder &folder) {
            folder.overlapped = {};
            folder.reading = ReadDirectoryChangesW(
                                 folder.handle, folder.buffer.data(), BUFFER_BYTES, TRUE, WATCHED, nullptr, &folder.overlapped,
                                 nullptr
                             ) != 0;
            return folder.reading;
        }

        void run() {
            Foundation::setCurrentThreadName("Fern Files");
            for (;;) {
                DWORD bytes = 0;
                ULONG_PTR key = 0;
                OVERLAPPED *overlapped = nullptr;
                const BOOL done = GetQueuedCompletionStatus(m_port, &bytes, &key, &overlapped, INFINITE);
                const DWORD error = done ? ERROR_SUCCESS : GetLastError();
                if (overlapped == nullptr) { return; } // the word to stop (or the port failed)
                Folder &folder = *reinterpret_cast<Folder *>(key);
                folder.reading = false;
                if (folder.handle == INVALID_HANDLE_VALUE) { continue; } // its watch has ended
                // A read that brought names; one that ended with nothing — more changed than
                // its buffer takes; one that failed — the folder is gone: removed, its drive
                // taken away.
                bool gone = !done && error != ERROR_NOTIFY_ENUM_DIR;
                if (done && bytes > 0) {
                    tell(folder);
                } else {
                    m_sink->lostTrack();
                }
                // The next read. A folder that is gone cannot begin one either.
                if (!gone && !read(folder)) {
                    m_sink->lostTrack();
                    gone = true;
                }
                if (gone) {
                    // Its watch ends here. The handle is let go at once: a removed folder stays
                    // on the disk for as long as one is held.
                    CloseHandle(folder.handle);
                    folder.handle = INVALID_HANDLE_VALUE;
                }
            }
        }

        // The records of a read that brought some: each is a name under the folder.
        void tell(const Folder &folder) {
            const BYTE *at = reinterpret_cast<const BYTE *>(folder.buffer.data());
            for (;;) {
                const FILE_NOTIFY_INFORMATION *record = reinterpret_cast<const FILE_NOTIFY_INFORMATION *>(at);
                const std::filesystem::path path =
                    folder.path / std::filesystem::path(std::wstring(record->FileName, record->FileNameLength / sizeof(WCHAR)));
                // A folder is told when it appears and when it goes. Its time alone is the
                // system's note of a name made or removed in it, and that name is told by itself.
                if (record->Action != FILE_ACTION_MODIFIED || !isFolder(path)) { m_sink->changed(path); }
                if (record->NextEntryOffset == 0) { break; }
                at += record->NextEntryOffset;
            }
        }

        std::shared_ptr<ChangeSink> m_sink;
        HANDLE m_port = nullptr;
        std::vector<std::unique_ptr<Folder>> m_folders; // by pointer: the system keeps the address of a pending read
        std::thread m_thread;
    };

} // namespace

std::unique_ptr<Backend> startSystemBackend(std::shared_ptr<ChangeSink> sink) {
    auto backend = std::make_unique<DirectoryChangesBackend>(std::move(sink));
    if (!backend->start()) { return nullptr; }
    return backend;
}

} // namespace Fern::Files
