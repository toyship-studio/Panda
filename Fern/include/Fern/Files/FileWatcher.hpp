#pragma once
// A watch over files and folders: what changes there is told to who asked — an editor that takes
// an edit made in another program, a tool that builds again what its sources changed.
//
// A watched folder is the system's to report, on a thread of its own: FSEvents on macOS,
// ReadDirectoryChangesW on Windows, inotify on Linux; where the system has nothing (or it would
// not start), the folder is read again once a second and compared with what it held. A watched
// single file is looked at four times a second on every platform — there are few of them. The
// changes wait in the watcher until its owner takes them — on the owner's thread, when it
// suits it: nothing is called back. A path is told once however many times it changed in
// between, and what happened to it is not said — written, made, removed, moved: the owner looks
// at the path as it is now.
#include <filesystem>
#include <memory>
#include <vector>

namespace Fern {

class FileWatcher final {
public:
    struct Changes {
        // Absolute, each once, in no order. They are real paths (links resolved, as
        // std::filesystem::weakly_canonical gives them): compare them with real paths.
        // A folder is told like a file — when it is made, removed, moved or renamed, not when
        // something changes inside it. A folder that was moved in whole may be told alone,
        // without what it holds: the owner reads a folder it is told of.
        std::vector<std::filesystem::path> paths;
        // The system lost track (too much changed at once): anything that is watched may have
        // changed, and `paths` is not all of it.
        bool everything = false;
    };

    enum class Backend {
        System, // the folders by what the platform has; the scan where it has nothing
        Scan,   // the scan for the folders too: a place whose changes the system does not report (a network drive)
    };

    // Watches the given paths: a folder with everything under it (a link to a folder is not
    // followed), a file by itself. A file that is not there yet is told when it appears; a
    // folder is watched from when it is there — one that is itself removed or moved away is not
    // followed, and the watcher is made anew for a folder made in its place. What changed a
    // moment before the watch began may be told too.
    explicit FileWatcher(const std::vector<std::filesystem::path> &paths, Backend backend = Backend::System);
    ~FileWatcher();
    FileWatcher(const FileWatcher &) = delete;
    FileWatcher &operator=(const FileWatcher &) = delete;

    // What changed since the last call; false — nothing did, `changes` is left empty.
    bool take(Changes &changes);

private:
    struct State;
    std::unique_ptr<State> m_state;
};

} // namespace Fern
