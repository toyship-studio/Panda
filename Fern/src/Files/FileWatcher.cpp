#include "Files/FileWatcherInternal.hpp"

#include <Foundation/Thread.hpp>

#include <chrono>
#include <condition_variable>
#include <map>
#include <system_error>
#include <thread>

#if defined(__APPLE__)
#    include <TargetConditionals.h>
#endif

namespace Fern {

namespace Files {

    namespace {

        // More paths than this waiting to be taken are not worth their names: the owner is told
        // that everything may have changed (a build tree written while nobody took the changes).
        constexpr size_t MOST_PENDING = 8192;

    } // namespace

    bool isUnder(const std::filesystem::path &path, const std::filesystem::path &folder) {
        const auto &text = path.native();
        const auto &root = folder.native();
        if (text.size() <= root.size() || text.compare(0, root.size(), root) != 0) { return false; }
        return text[root.size()] == std::filesystem::path::preferred_separator ||
               (!root.empty() && root.back() == std::filesystem::path::preferred_separator);
    }

    ChangeSink::ChangeSink(const std::vector<std::filesystem::path> &paths) {
        for (const std::filesystem::path &path : paths) {
            if (path.empty()) { continue; }
            std::error_code error;
            std::filesystem::path real = std::filesystem::weakly_canonical(path, error);
            if (error) { real = std::filesystem::absolute(path, error); }
            if (error) { continue; }
            if (std::filesystem::is_directory(real, error)) {
                m_folders.push_back(real);
            } else {
                m_files.push_back(real);
            }
        }
    }

    bool ChangeSink::watches(const std::filesystem::path &path) const {
        for (const std::filesystem::path &folder : m_folders) {
            if (path == folder || isUnder(path, folder)) { return true; }
        }
        for (const std::filesystem::path &file : m_files) {
            if (path == file) { return true; }
        }
        return false;
    }

    void ChangeSink::changed(const std::filesystem::path &path) {
        if (!watches(path)) { return; }
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_everything) { return; }
        m_pending.insert(path);
        if (m_pending.size() > MOST_PENDING) {
            m_pending.clear();
            m_everything = true;
        }
    }

    void ChangeSink::lostTrack() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.clear();
        m_everything = true;
    }

    bool ChangeSink::take(FileWatcher::Changes &changes) {
        changes.paths.clear();
        changes.everything = false;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_pending.empty() && !m_everything) { return false; }
        changes.paths.assign(m_pending.begin(), m_pending.end());
        changes.everything = m_everything;
        m_pending.clear();
        m_everything = false;
        return true;
    }

    /* ------------------------------ the scan ------------------------------ */

    namespace {

        // How often a tree of folders is read, and how often a few single files are looked at.
        constexpr std::chrono::milliseconds SCAN_FOLDERS_EVERY(1000);
        constexpr std::chrono::milliseconds SCAN_FILES_EVERY(250);

        // The portable backend: a thread reads its folders and its files again and again and
        // tells what is not as it was — a file by its time and its size, a folder by being there.
        class ScanBackend final : public Backend {
        public:
            ScanBackend(
                std::shared_ptr<ChangeSink> sink, std::vector<std::filesystem::path> folders,
                std::vector<std::filesystem::path> files
            )
                : m_sink(std::move(sink))
                , m_folders(std::move(folders))
                , m_files(std::move(files)) {
                read(m_seen); // what is there now is not a change
                m_thread = std::thread([this] { run(); });
            }

            ~ScanBackend() override {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_stop = true;
                }
                m_wake.notify_all();
                m_thread.join();
            }

        private:
            struct Stamp {
                std::filesystem::file_time_type time;
                std::uintmax_t size = 0;
                bool operator==(const Stamp &other) const {
                    return time == other.time && size == other.size;
                }
            };
            using Files = std::map<std::filesystem::path, Stamp>;

            static void note(const std::filesystem::path &file, Files &files) {
                std::error_code error;
                Stamp stamp;
                stamp.time = std::filesystem::last_write_time(file, error);
                if (error) { return; } // gone while it was read
                stamp.size = std::filesystem::file_size(file, error);
                files[file] = stamp;
            }

            void read(Files &files) const {
                for (const std::filesystem::path &folder : m_folders) {
                    std::error_code error;
                    std::filesystem::recursive_directory_iterator it(
                        folder, std::filesystem::directory_options::skip_permission_denied, error
                    );
                    for (; !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
                        std::error_code typeError;
                        if (it->is_regular_file(typeError)) {
                            note(it->path(), files);
                        } else if (it->is_directory(typeError)) {
                            // Only that it is there: its time changes with every name made in it.
                            files[it->path()] = Stamp();
                        }
                    }
                }
                for (const std::filesystem::path &file : m_files) {
                    std::error_code error;
                    if (std::filesystem::is_regular_file(file, error)) { note(file, files); }
                }
            }

            void run() {
                Foundation::setCurrentThreadName("Fern Files Scan");
                const std::chrono::milliseconds every = m_folders.empty() ? SCAN_FILES_EVERY : SCAN_FOLDERS_EVERY;
                std::unique_lock<std::mutex> lock(m_mutex);
                while (!m_wake.wait_for(lock, every, [this] { return m_stop; })) {
                    lock.unlock();
                    Files now;
                    read(now);
                    for (const auto &[file, stamp] : now) {
                        const auto before = m_seen.find(file);
                        if (before == m_seen.end() || !(before->second == stamp)) { m_sink->changed(file); }
                    }
                    for (const auto &[file, stamp] : m_seen) {
                        if (!now.contains(file)) { m_sink->changed(file); }
                    }
                    m_seen = std::move(now);
                    lock.lock();
                }
            }

            std::shared_ptr<ChangeSink> m_sink;
            std::vector<std::filesystem::path> m_folders, m_files;
            Files m_seen;
            std::thread m_thread;
            std::mutex m_mutex;
            std::condition_variable m_wake;
            bool m_stop = false;
        };

    } // namespace

    std::unique_ptr<Backend> startScanBackend(
        std::shared_ptr<ChangeSink> sink, std::vector<std::filesystem::path> folders, std::vector<std::filesystem::path> files
    ) {
        return std::make_unique<ScanBackend>(std::move(sink), std::move(folders), std::move(files));
    }

#if !((defined(__APPLE__) && TARGET_OS_OSX) || defined(_WIN32) || defined(__linux__))
    // A platform without a watch of its own (a phone): the scan watches there.
    std::unique_ptr<Backend> startSystemBackend(std::shared_ptr<ChangeSink>) {
        return nullptr;
    }
#endif

} // namespace Files

struct FileWatcher::State {
    std::shared_ptr<Files::ChangeSink> sink;
    std::unique_ptr<Files::Backend> system; // the folders, by the platform
    std::unique_ptr<Files::Backend> scan;   // the single files — and the folders the platform does not watch
};

FileWatcher::FileWatcher(const std::vector<std::filesystem::path> &paths, Backend backend)
    : m_state(std::make_unique<State>()) {
    m_state->sink = std::make_shared<Files::ChangeSink>(paths);
    const std::vector<std::filesystem::path> &folders = m_state->sink->folders();
    const std::vector<std::filesystem::path> &files = m_state->sink->files();
    if (backend == Backend::System && !folders.empty()) { m_state->system = Files::startSystemBackend(m_state->sink); }
    // A backend of a system watches folders: the single files are the scan's on every platform,
    // and so are the folders when the system's backend is not there.
    const bool scanFolders = !m_state->system && !folders.empty();
    if (scanFolders || !files.empty()) {
        m_state->scan = Files::startScanBackend(
            m_state->sink, scanFolders ? folders : std::vector<std::filesystem::path>(), files
        );
    }
}

FileWatcher::~FileWatcher() = default;

bool FileWatcher::take(Changes &changes) {
    return m_state->sink->take(changes);
}

} // namespace Fern
