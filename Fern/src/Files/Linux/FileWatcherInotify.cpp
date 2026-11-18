// The file watch of Linux: inotify. A watch of inotify is of one folder, not of what lies under
// it: the backend puts one on every folder of the watched trees and keeps them with the trees —
// on a folder that is made or moved in, off a folder that is moved away (the watch of a removed
// one the system takes off itself). A thread of the backend reads the events and hands the
// names to the sink.
#include "Files/FileWatcherInternal.hpp"

#include <Foundation/Logger.hpp>
#include <Foundation/Thread.hpp>

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <map>
#include <thread>

namespace Fern::Files {

namespace {

    // What is asked of a folder: the names made, removed and moved in it, and its files written —
    // IN_CLOSE_WRITE also for a file written through a mapping, which gives no IN_MODIFY, and
    // IN_ATTRIB for a file whose time was set by hand. A link to a folder is not followed.
    constexpr uint32_t WATCHED = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_MODIFY | IN_CLOSE_WRITE | IN_ATTRIB |
                                 IN_ONLYDIR | IN_DONT_FOLLOW;

    class InotifyBackend final : public Backend {
    public:
        explicit InotifyBackend(std::shared_ptr<ChangeSink> sink)
            : m_sink(std::move(sink)) {}

        bool start() {
            m_inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
            m_stop = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
            if (m_inotify < 0 || m_stop < 0) { return false; }
            for (const std::filesystem::path &folder : m_sink->folders()) {
                // A tree with more folders than the system gives watches to a user is not this
                // backend's to watch: the scan reads it.
                if (!watchTree(folder, false)) {
                    LOG_WARN(
                        "File watch: %s has more folders than the system gives inotify watches "
                        "(fs.inotify.max_user_watches): it is read once a second instead",
                        folder.c_str()
                    );
                    return false;
                }
            }
            m_thread = std::thread([this] { run(); });
            return true;
        }

        ~InotifyBackend() override {
            if (m_thread.joinable()) {
                const uint64_t word = 1;
                const ssize_t written = write(m_stop, &word, sizeof(word)); // the word to stop
                (void)written;
                m_thread.join();
            }
            if (m_inotify >= 0) { close(m_inotify); } // its watches go with it
            if (m_stop >= 0) { close(m_stop); }
        }

    private:
        // A watch on one folder; false — the user has no watches left. A folder that is gone
        // already, or is not to be read, is left without one: there is nothing to tell of it.
        bool watch(const std::filesystem::path &folder) {
            const int descriptor = inotify_add_watch(m_inotify, folder.c_str(), WATCHED);
            if (descriptor < 0) { return errno != ENOSPC; }
            m_watched[descriptor] = folder;
            return true;
        }

        // A watch on the folder and on every folder under it; false — the user has no watches
        // left. `tell`: the folder appeared a moment ago, and what was put into it before its
        // watch was there came without an event — what is found in it is told.
        bool watchTree(const std::filesystem::path &folder, bool tell) {
            if (!watch(folder)) { return false; }
            std::error_code error;
            std::filesystem::recursive_directory_iterator it(
                folder, std::filesystem::directory_options::skip_permission_denied, error
            );
            for (; !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
                if (tell) { m_sink->changed(it->path()); }
                std::error_code typeError;
                if (it->is_directory(typeError) && !it->is_symlink(typeError) && !watch(it->path())) { return false; }
            }
            return true;
        }

        // The watches off the folder and off every folder under it. A watch stays with its
        // folder wherever that is moved: left on, it would tell of names that are no longer here.
        void unwatchTree(const std::filesystem::path &folder) {
            for (auto it = m_watched.begin(); it != m_watched.end();) {
                if (it->second == folder || isUnder(it->second, folder)) {
                    inotify_rm_watch(m_inotify, it->first);
                    it = m_watched.erase(it);
                } else {
                    ++it;
                }
            }
        }

        void outOfWatches(const std::filesystem::path &folder) {
            m_sink->lostTrack();
            if (m_saidOutOfWatches) { return; }
            m_saidOutOfWatches = true;
            LOG_WARN(
                "File watch: the system gives no more inotify watches (fs.inotify.max_user_watches): changes under %s are "
                "not seen",
                folder.c_str()
            );
        }

        void run() {
            Foundation::setCurrentThreadName("Fern Files");
            // The events of one read: each is a header and the name it is about.
            alignas(inotify_event) char buffer[32 * 1024];
            pollfd waited[2] = {{m_inotify, POLLIN, 0}, {m_stop, POLLIN, 0}};
            for (;;) {
                if (poll(waited, 2, -1) < 0) {
                    if (errno == EINTR) { continue; }
                    break;
                }
                if (waited[1].revents != 0) { return; } // the word to stop
                if ((waited[0].revents & POLLIN) == 0) {
                    if (waited[0].revents != 0) { break; }
                    continue;
                }
                const ssize_t size = read(m_inotify, buffer, sizeof(buffer));
                if (size <= 0) {
                    if (size < 0 && (errno == EINTR || errno == EAGAIN)) { continue; }
                    break;
                }
                for (ssize_t at = 0; at < size;) {
                    const inotify_event *event = reinterpret_cast<const inotify_event *>(buffer + at);
                    at += static_cast<ssize_t>(sizeof(inotify_event) + event->len);
                    handle(*event);
                }
            }
            // The system took its events away: the watch ends, and its owner looks at what is there.
            const int error = errno;
            m_sink->lostTrack();
            LOG_ERROR("File watch: inotify failed (error %d): changes of files are no longer seen", error);
        }

        void handle(const inotify_event &event) {
            if ((event.mask & IN_Q_OVERFLOW) != 0) {
                // The system dropped events: the names are lost, and with them may be the news
                // of new folders — the trees are walked again for the folders without a watch.
                m_sink->lostTrack();
                for (const std::filesystem::path &folder : m_sink->folders()) {
                    if (!watchTree(folder, false)) { outOfWatches(folder); }
                }
                return;
            }
            const auto watched = m_watched.find(event.wd);
            if (watched == m_watched.end()) { return; } // of a watch taken off: read before it went
            if ((event.mask & IN_IGNORED) != 0) {
                // The system took the watch off: the folder is removed. The folder above told
                // so; a watched folder itself has none above it here, and its watch ends.
                const std::vector<std::filesystem::path> &roots = m_sink->folders();
                if (std::find(roots.begin(), roots.end(), watched->second) != roots.end()) { m_sink->lostTrack(); }
                m_watched.erase(watched);
                return;
            }
            if (event.len == 0) { return; } // of the folder itself, not of a name in it
            const char *name = event.name;
            const std::filesystem::path path = watched->second / std::filesystem::path(name);
            m_sink->changed(path);
            if ((event.mask & IN_ISDIR) == 0) { return; }
            if ((event.mask & (IN_CREATE | IN_MOVED_TO)) != 0) {
                if (!watchTree(path, true)) { outOfWatches(path); }
            } else if ((event.mask & IN_MOVED_FROM) != 0) {
                unwatchTree(path);
            }
        }

        std::shared_ptr<ChangeSink> m_sink;
        int m_inotify = -1;
        int m_stop = -1; // an eventfd: written when the thread is to end
        // The folder of every watch. The thread's own once it runs.
        std::map<int, std::filesystem::path> m_watched;
        bool m_saidOutOfWatches = false;
        std::thread m_thread;
    };

} // namespace

std::unique_ptr<Backend> startSystemBackend(std::shared_ptr<ChangeSink> sink) {
    auto backend = std::make_unique<InotifyBackend>(std::move(sink));
    if (!backend->start()) { return nullptr; }
    return backend;
}

} // namespace Fern::Files
