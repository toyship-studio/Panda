#pragma once

// The editor updater (context/infrastructure.md, section 5), the Zed model: a check at startup and
// once an hour against the channel manifest, the download of a newer build with a resume and a
// SHA-256 check, the install in the background, and a restart into the new version. The UI (the
// indicator of the toolbar and of the start screen, the Help menu, the About window, the UPDATES
// settings section) reads the state and asks for the two actions: check and restart.
//
// A build knows its channel (ENGINE_RELEASE_CHANNEL) and its number (ENGINE_BUILD_NUMBER); a local
// build has neither and never updates. The manifest comes from the portal, or from the file in
// panda-builds when the portal does not answer. The build is downloaded through the counted
// address of the portal first and through the addresses of the manifest after that.
//
// Install, the same on every platform: the new build is unpacked (the bundle of the dmg is copied)
// next to the user data, and the install folder is swapped when the editor exits (finishOnExit:
// macOS and Linux by themselves, Windows through PandaUpdateHelper, which waits for the process to
// end). The running editor never reads a file of the new version. Every step keeps the current
// install usable: nothing is moved before the whole new build is on disk, and a failed swap is
// rolled back.

#include <Fern/Net/Http.hpp>
#include <Panda/Assets/Base/Path.hpp>
#include <PandaAsync/PandaAsync.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Panda {

struct UpdateState {
    enum class Kind {
        Idle,
        Checking,
        Downloading,
        Installing,
        Ready,    // installed: a restart runs the new version
        UpToDate, // a manual check found nothing (goes away by itself)
        Updated,  // the first start of a new version
        Error,    // a manual check or its download failed; automatic ones are quiet
    };
    Kind kind = Kind::Idle;
    std::string version;   // the version the state is about
    uint32_t build = 0;    // its build number
    float progress = -1.f; // Downloading: 0..1, -1 unknown
    std::string message;   // Error: one line
    std::string notes;     // the release notes of the update (the plain text of the manifest)
    uint64_t revision = 0; // grows with every change
};

class EditorUpdater final {
public:
    EditorUpdater();
    ~EditorUpdater();
    EditorUpdater(const EditorUpdater &) = delete;
    EditorUpdater &operator=(const EditorUpdater &) = delete;

    // A release build with a channel; a local build has nothing to compare with.
    static bool isEnabled();
    // "0.1.0-preview.7", or "0.1.0" for a local build.
    static std::string versionString();
    static std::string platformString(); // "macOS arm64"

    // After the settings are read: removes the leftovers of the previous update, reports a
    // finished one, and starts the first check when automatic updates are on.
    void start(bool autoUpdate);
    void setAutoUpdate(bool on);
    // A check now. Manual (Help → Check for Updates): errors and "up to date" are shown; automatic
    // checks stay quiet when they fail.
    void check(bool manual);
    void tick(double deltaTime);

    const UpdateState &state() const {
        return m_state;
    }
    bool isDismissed() const {
        return m_dismissedRevision == m_state.revision;
    }
    void dismiss() {
        m_dismissedRevision = m_state.revision;
    }
    bool hasReadyUpdate() const {
        return m_state.kind == UpdateState::Kind::Ready;
    }
    std::string releaseNotesUrl() const;

    // main(): finishes a staged update after the editor exited and its files are closed. macOS and
    // Linux swap the install folder here, Windows hands over to PandaUpdateHelper, which waits for
    // this process; all start the new editor when a relaunch was asked. Returns true when it took
    // the restart over.
    static bool finishOnExit(bool relaunch);

private:
    struct InstallResult;

    void setState(UpdateState::Kind kind, std::string version = {}, uint32_t build = 0, std::string notes = {},
                  std::string message = {});
    void fail(const std::string &message);
    void fetchManifest(size_t attempt);
    void onManifest(const std::string &json);
    void download();
    void install();
    Fern::Net::Client &client();

    std::unique_ptr<Fern::Net::Client> m_client;
    std::shared_ptr<bool> m_alive; // completions of jobs outlive nothing: they check it
    UpdateState m_state;
    uint64_t m_dismissedRevision = ~uint64_t(0);
    bool m_autoUpdate = true;
    bool m_started = false;
    bool m_busy = false;   // a check, a download or an install is in flight
    bool m_manual = false; // the check in flight was asked for by hand
    double m_pollTimer = 0.0;
    double m_transientTimer = 0.0;

    // The update in flight
    std::string m_version;
    uint32_t m_build = 0;
    std::string m_notes;
    std::vector<std::string> m_urls;
    size_t m_urlIndex = 0;
    uint64_t m_size = 0;
    std::string m_sha256;
    path_t m_archive;
    Fern::Net::RequestHandle m_request;
    std::string m_reportOutcome; // the result of the last download, told to the portal once
    std::string m_reportStorage;
};

} // namespace Panda
