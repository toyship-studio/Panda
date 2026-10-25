#include "App/EditorUpdater.hpp"

#include "Project/System/Portal.hpp"
#include "Project/System/SystemTools.hpp"
#include "Project/System/ZipArchive.hpp"

#include <Fern/Fern.hpp>
#include <Foundation/Hash/Sha256.hpp>
#include <Foundation/Logger.hpp>
#include <Foundation/PlatformDetection.hpp>
#include <Panda/Application/Application.hpp>
#include <Panda/Base/Version.hpp>
#include <Rain/Coders/JsonDecoder.hpp>
#include <Rain/Coders/JsonEncoder.hpp>
#include <Rain/Rain.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace Panda {

namespace {

    constexpr double kPollIntervalSeconds = 60.0 * 60.0;
    constexpr double kUpToDateSeconds = 6.0;
    constexpr const char *kPortalApi = Portal::kApi;
    constexpr const char *kPortalDownloads = Portal::kDownloads;
    constexpr const char *kPortalDownloadPage = Portal::kDownloadPage;
    constexpr const char *kFallbackManifests = "https://raw.githubusercontent.com/toyship-studio/panda-builds/main/manifests";

    // The channel manifest (context/infrastructure.md, "Manifest").
    struct ManifestFile final : public Rain::Codable {
        std::string os;
        std::string arch;
        std::string kind;
        uint64_t size = 0;
        std::string sha256;
        std::vector<std::string> urls;

        RAIN_FIELDS_BEGIN(ManifestFile)
        RAIN_FIELD(os)
        RAIN_FIELD(arch)
        RAIN_FIELD(kind)
        RAIN_FIELD(size)
        RAIN_FIELD(sha256)
        RAIN_FIELD(urls)
        RAIN_FIELDS_END
    };

    struct Manifest final : public Rain::Codable {
        std::string channel;
        std::string version;
        uint32_t build = 0;
        std::string commit;
        std::string date;
        std::string notes;
        std::vector<ManifestFile> files;

        RAIN_FIELDS_BEGIN(Manifest)
        RAIN_FIELD(channel)
        RAIN_FIELD(version)
        RAIN_FIELD(build)
        RAIN_FIELD(commit)
        RAIN_FIELD(date)
        RAIN_FIELD(notes)
        RAIN_FIELD(files)
        RAIN_FIELDS_END
    };

    // updates/pending.json: the update that is staged for the swap on exit. Read at startup to
    // report the new version or to offer the restart again.
    struct PendingUpdate final : public Rain::Codable {
        uint32_t build = 0;
        std::string version;
        std::string notes;
        std::string staged; // the unpacked build: PandaEditor.app on macOS, the PandaEditor folder elsewhere

        RAIN_FIELDS_BEGIN(PendingUpdate)
        RAIN_FIELD(build)
        RAIN_FIELD(version)
        RAIN_FIELD(notes)
        RAIN_FIELD(staged)
        RAIN_FIELDS_END
    };

    const char *hostOs() {
#if defined(PLATFORM_MACOS)
        return "macos";
#elif defined(PLATFORM_WINDOWS)
        return "windows";
#elif defined(PLATFORM_LINUX)
        return "linux";
#else
        return "unknown";
#endif
    }

    const char *hostArch() {
#if defined(__aarch64__) || defined(_M_ARM64)
        return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
        return "x86_64";
#else
        return "unknown";
#endif
    }

    const char *archiveKind() {
#if defined(PLATFORM_MACOS)
        return "dmg";
#elif defined(PLATFORM_WINDOWS)
        return "zip";
#else
        return "tar.gz";
#endif
    }

    path_t updatesDirectory() {
        path_t directory = SystemTools::userDataDirectory();
        if (directory.empty()) { return {}; }
        directory /= "updates";
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        return error ? path_t() : directory;
    }

    // The folder (the bundle on macOS) the running editor was started from.
    path_t installDirectory() {
#if defined(PLATFORM_MACOS)
        return Fern::getStaticResourcesPath().parent_path().parent_path();
#elif defined(PLATFORM_LINUX)
        std::error_code error;
        const path_t executable = std::filesystem::read_symlink("/proc/self/exe", error);
        return error ? path_t() : executable.parent_path();
#else
        return Fern::getStaticResourcesPath();
#endif
    }

    path_t pendingPath() {
        const path_t directory = updatesDirectory();
        return directory.empty() ? path_t() : directory / "pending.json";
    }

    template<typename T>
    bool readJson(const path_t &path, T &data) {
        std::ifstream file(path);
        if (!file.is_open()) { return false; }
        Rain::JsonDecoder decoder;
        Rain::Decoder *decoderPtr = &decoder;
        return decoderPtr->decode(file, data);
    }

    template<typename T>
    bool writeJson(const path_t &path, const T &data) {
        std::ofstream file(path);
        if (!file.is_open()) { return false; }
        Rain::JsonEncoder encoder;
        Rain::Encoder *encoderPtr = &encoder;
        encoderPtr->encode(file, data);
        return true;
    }

    std::string storageOf(const std::string &url) {
        return url.find("github") != std::string::npos ? "github" : "s3";
    }

    std::string fileNameOf(const std::string &url) {
        const size_t query = url.find('?');
        const std::string path = query == std::string::npos ? url : url.substr(0, query);
        const size_t slash = path.rfind('/');
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    // Moves a folder; across file systems it copies and removes (ditto on macOS keeps the bundle
    // exactly as it was signed).
    bool moveDirectory(const path_t &from, const path_t &to, std::string &error) {
        std::error_code code;
        std::filesystem::rename(from, to, code);
        if (!code) { return true; }
#if defined(PLATFORM_MACOS)
        std::string output;
        if (!SystemTools::runCommand("/usr/bin/ditto", {from.string(), to.string()}, {}, [&output](std::string_view line) {
                output += std::string(line) + "\n";
            })) {
            error = output.empty() ? "ditto failed" : output;
            std::filesystem::remove_all(to, code);
            return false;
        }
#else
        std::filesystem::copy(from, to, std::filesystem::copy_options::recursive | std::filesystem::copy_options::copy_symlinks, code);
        if (code) {
            error = code.message();
            std::filesystem::remove_all(to, code);
            return false;
        }
#endif
        std::filesystem::remove_all(from, code);
        return true;
    }

    // The swap on the previous exit may have left the old install behind.
    void removeOldInstall() {
        const path_t install = installDirectory();
        if (install.empty()) { return; }
        path_t old = install;
        old += ".old";
        std::error_code error;
        if (std::filesystem::exists(old, error)) { std::filesystem::remove_all(old, error); }
    }

} // namespace

struct EditorUpdater::InstallResult {
    bool ok = false;
    std::string message;
    std::string staged;
};

EditorUpdater::EditorUpdater()
    : m_alive(std::make_shared<bool>(true)) {}

EditorUpdater::~EditorUpdater() {
    *m_alive = false;
    m_request.cancel();
}

bool EditorUpdater::isEnabled() {
    return ENGINE_BUILD_NUMBER != 0 && ENGINE_RELEASE_CHANNEL[0] != '\0';
}

std::string EditorUpdater::versionString() {
    std::string version = ENGINE_VERSION;
    if (isEnabled()) { version += std::string("-") + ENGINE_RELEASE_CHANNEL + "." + std::to_string(ENGINE_BUILD_NUMBER); }
    return version;
}

std::string EditorUpdater::platformString() {
#if defined(PLATFORM_MACOS)
    const char *os = "macOS";
#elif defined(PLATFORM_WINDOWS)
    const char *os = "Windows";
#elif defined(PLATFORM_LINUX)
    const char *os = "Linux";
#else
    const char *os = "unknown";
#endif
    return std::string(os) + " " + hostArch();
}

std::string EditorUpdater::releaseNotesUrl() const {
    return kPortalDownloadPage;
}

Fern::Net::Client &EditorUpdater::client() {
    if (!m_client) {
        Fern::Net::ClientDesc desc;
        desc.userAgent = std::string("PandaEngine/") + versionString() + " (" + hostOs() + "; " + hostArch() + ")";
        m_client = std::make_unique<Fern::Net::Client>(Application::get()->getScheduler(), desc);
    }
    return *m_client;
}

void EditorUpdater::setState(UpdateState::Kind kind, std::string version, uint32_t build, std::string notes, std::string message) {
    m_state.kind = kind;
    m_state.version = std::move(version);
    m_state.build = build;
    m_state.notes = std::move(notes);
    m_state.message = std::move(message);
    m_state.progress = -1.f;
    ++m_state.revision;
    m_transientTimer = 0.0;
}

void EditorUpdater::start(bool autoUpdate) {
    m_autoUpdate = autoUpdate;
    m_started = true;
    removeOldInstall();

    const path_t pending = pendingPath();
    PendingUpdate update;
    if (!pending.empty() && readJson(pending, update)) {
        std::error_code error;
        const path_t updates = updatesDirectory();
        if (update.build == ENGINE_BUILD_NUMBER) {
            // This is the new version: say so once, and clean up.
            std::filesystem::remove(pending, error);
            if (!update.staged.empty()) { std::filesystem::remove_all(path_t(update.staged).parent_path(), error); }
            setState(UpdateState::Kind::Updated, update.version, update.build, update.notes);
        } else if (update.build > ENGINE_BUILD_NUMBER && !update.staged.empty() && std::filesystem::is_directory(update.staged, error)) {
            // Staged before, not yet swapped in: the restart is still the only step left.
            setState(UpdateState::Kind::Ready, update.version, update.build, update.notes);
        } else {
            std::filesystem::remove(pending, error);
            if (!updates.empty()) { std::filesystem::remove_all(updates / "stage", error); }
        }
    }
    if (!isEnabled()) {
        LOG_INFO("Updater: a local build does not update itself");
        return;
    }
    if (m_autoUpdate) { check(false); }
}

void EditorUpdater::setAutoUpdate(bool on) {
    if (on && !m_autoUpdate && m_started && !m_busy) { m_pollTimer = kPollIntervalSeconds; } // the next tick checks
    m_autoUpdate = on;
}

void EditorUpdater::tick(double deltaTime) {
    if (m_state.kind == UpdateState::Kind::UpToDate) {
        m_transientTimer += deltaTime;
        if (m_transientTimer >= kUpToDateSeconds) { setState(UpdateState::Kind::Idle); }
    }
    if (!isEnabled() || !m_autoUpdate || m_busy || !m_started) { return; }
    if (m_state.kind == UpdateState::Kind::Ready) { return; } // nothing newer is installed before the restart
    m_pollTimer += deltaTime;
    if (m_pollTimer >= kPollIntervalSeconds) {
        m_pollTimer = 0.0;
        check(false);
    }
}

void EditorUpdater::check(bool manual) {
    if (!isEnabled()) { return; }
    if (manual) { m_dismissedRevision = ~uint64_t(0); }
    if (m_busy) {
        m_manual = m_manual || manual;
        return;
    }
    if (m_state.kind == UpdateState::Kind::Ready) {
        // The installed update waits for the restart; a check shows it again.
        if (manual) { ++m_state.revision; }
        return;
    }
    m_manual = manual;
    m_busy = true;
    m_pollTimer = 0.0;
    setState(UpdateState::Kind::Checking);
    fetchManifest(0);
}

void EditorUpdater::fail(const std::string &message) {
    m_busy = false;
    m_request = Fern::Net::RequestHandle();
    if (m_manual) {
        LOG_ERROR("Updater: %s", message.c_str());
        setState(UpdateState::Kind::Error, m_version, m_build, {}, message);
    } else {
        LOG_INFO("Updater: %s", message.c_str());
        setState(UpdateState::Kind::Idle);
    }
}

void EditorUpdater::fetchManifest(size_t attempt) {
    Fern::Net::Request request;
    if (attempt == 0) {
        // The portal counts the check and learns how the last download went (section 6).
        request.url = std::string(kPortalApi) + "/releases/" + ENGINE_RELEASE_CHANNEL + "/latest?from=editor&current=" +
                      std::to_string(ENGINE_BUILD_NUMBER) + "&os=" + hostOs() + "&arch=" + hostArch();
        if (!m_reportOutcome.empty()) {
            request.url += "&downloadOutcome=" + m_reportOutcome + "&downloadStorage=" + m_reportStorage;
        }
    } else {
        request.url = std::string(kFallbackManifests) + "/" + ENGINE_RELEASE_CHANNEL + ".json";
    }
    request.timeouts.totalSeconds = 60;
    std::shared_ptr<bool> alive = m_alive;
    m_request = client().send(request, [this, alive, attempt](Fern::Net::Response &&response) {
        if (!*alive) { return; }
        if (response.ok()) {
            if (attempt == 0) { m_reportOutcome.clear(); }
            onManifest(response.bodyText());
            return;
        }
        if (attempt == 0) {
            LOG_INFO("Updater: the portal did not answer (%s, status %d), trying the mirror", Fern::Net::errorName(response.error), response.status);
            fetchManifest(1);
            return;
        }
        fail(std::string("The update server did not answer (") + Fern::Net::errorName(response.error) + ")");
    });
}

void EditorUpdater::onManifest(const std::string &json) {
    Manifest manifest;
    {
        std::istringstream stream(json);
        Rain::JsonDecoder decoder;
        Rain::Decoder *decoderPtr = &decoder;
        if (!decoderPtr->decode(stream, manifest) || manifest.build == 0 || manifest.version.empty()) {
            fail("The update manifest could not be read");
            return;
        }
    }
    if (manifest.build <= ENGINE_BUILD_NUMBER) {
        m_busy = false;
        setState(m_manual ? UpdateState::Kind::UpToDate : UpdateState::Kind::Idle, versionString(), ENGINE_BUILD_NUMBER);
        return;
    }
    const ManifestFile *file = nullptr;
    for (const ManifestFile &candidate : manifest.files) {
        if (candidate.os == hostOs() && candidate.arch == hostArch() && candidate.kind == archiveKind()) { file = &candidate; }
    }
    if (!file || file->urls.empty()) {
        fail("Build " + std::to_string(manifest.build) + " has no file for " + platformString());
        return;
    }
    m_version = manifest.version;
    m_build = manifest.build;
    m_notes = manifest.notes;
    m_size = file->size;
    m_sha256 = file->sha256;
    const path_t updates = updatesDirectory();
    if (updates.empty()) {
        fail("The user data folder is not available");
        return;
    }
    m_archive = updates / fileNameOf(file->urls.front());
    m_urls.clear();
    m_urls.push_back(std::string(kPortalDownloads) + "/" + ENGINE_RELEASE_CHANNEL + "/" + hostOs() + "/" + hostArch() + "?from=editor");
    m_urls.insert(m_urls.end(), file->urls.begin(), file->urls.end());
    m_urlIndex = 0;
    setState(UpdateState::Kind::Downloading, m_version, m_build, m_notes);
    m_state.progress = 0.f;
    download();
}

void EditorUpdater::download() {
    Fern::Net::Request request;
    request.url = m_urls[m_urlIndex];
    request.downloadTo = m_archive;
    request.resume = true; // a broken download continues; another storage restarts the file
    request.expectedSize = m_size;
    request.expectedSha256 = Foundation::Sha256Digest::fromHex(m_sha256);
    request.timeouts.idleSeconds = 60;
    std::shared_ptr<bool> alive = m_alive;
    m_request = client().send(
        request,
        [this, alive](Fern::Net::Response &&response) {
            if (!*alive) { return; }
            if (response.ok()) {
                m_reportOutcome = "ok";
                m_reportStorage = storageOf(response.finalUrl);
                install();
                return;
            }
            LOG_WARN("Updater: download from %s failed: %s (status %d)", m_urls[m_urlIndex].c_str(), Fern::Net::errorName(response.error), response.status);
            if (m_urlIndex + 1 < m_urls.size()) {
                ++m_urlIndex;
                download();
                return;
            }
            m_reportOutcome = "failed";
            m_reportStorage = storageOf(response.finalUrl.empty() ? m_urls[m_urlIndex] : response.finalUrl);
            fail(std::string("The download failed: ") + Fern::Net::errorName(response.error));
        },
        [this, alive](const Fern::Net::Progress &progress) {
            if (!*alive || m_state.kind != UpdateState::Kind::Downloading) { return; }
            const float fraction = progress.total && *progress.total > 0 ? float(double(progress.received) / double(*progress.total)) : -1.f;
            if (fraction != m_state.progress) {
                m_state.progress = fraction;
                ++m_state.revision;
            }
        }
    );
}

void EditorUpdater::install() {
    setState(UpdateState::Kind::Installing, m_version, m_build, m_notes);
    const path_t archive = m_archive;
    const path_t updates = updatesDirectory();
    const path_t install = installDirectory();
    std::shared_ptr<bool> alive = m_alive;
    Application::get()->getScheduler().submit<InstallResult>(
        PandaAsync::JobDesc{"update install", PandaAsync::Priority::Background},
        [archive, updates, install](PandaAsync::Context &) -> InstallResult {
            InstallResult result;
            std::error_code error;
#if defined(PLATFORM_MACOS)
            // The bundle of the image is copied to the stage as it was signed (ditto keeps every
            // attribute); the swap with the running bundle happens on exit, like on Linux.
            const path_t stage = updates / "stage";
            const path_t mount = updates / "mount";
            std::filesystem::remove_all(stage, error);
            std::filesystem::create_directories(stage, error);
            std::filesystem::create_directories(mount, error);
            std::string output;
            auto collect = [&output](std::string_view line) { output += std::string(line) + "\n"; };
            SystemTools::runCommand("/usr/bin/hdiutil", {"detach", mount.string(), "-quiet"}, {}, {}); // a stale mount
            if (!SystemTools::runCommand("/usr/bin/hdiutil", {"attach", "-nobrowse", "-readonly", "-mountpoint", mount.string(), archive.string()}, {}, collect)) {
                result.message = "The disk image could not be opened: " + output;
                return result;
            }
            const path_t source = mount / install.filename();
            const path_t staged = stage / install.filename();
            bool copied = false;
            if (std::filesystem::is_directory(source, error)) {
                output.clear();
                copied = SystemTools::runCommand("/usr/bin/ditto", {source.string(), staged.string()}, {}, collect);
                if (!copied) { result.message = "The bundle could not be copied: " + output; }
            } else {
                result.message = "The disk image holds no " + install.filename().string();
            }
            SystemTools::runCommand("/usr/bin/hdiutil", {"detach", mount.string(), "-quiet"}, {}, {});
            if (!copied || !std::filesystem::is_regular_file(staged / "Contents" / "MacOS" / "PandaEditor", error)) {
                if (copied) { result.message = "The copied bundle is incomplete"; }
                return result;
            }
            std::filesystem::remove(archive, error);
            result.staged = staged.string();
            result.ok = true;
            return result;
#else
            const path_t stage = updates / "stage";
            std::filesystem::remove_all(stage, error);
            std::filesystem::create_directories(stage, error);
            std::string output;
            auto collect = [&output](std::string_view line) { output += std::string(line) + "\n"; };
#    if defined(PLATFORM_WINDOWS)
            std::string zipError;
            const bool unpacked = ZipArchive::extract(archive, stage, zipError);
            if (!unpacked) { output = zipError; }
#    else
            const bool unpacked = SystemTools::runCommand("/usr/bin/tar", {"-xzf", archive.string(), "-C", stage.string()}, {}, collect);
#    endif
            if (!unpacked) {
                result.message = "The archive could not be unpacked: " + output;
                return result;
            }
            const path_t staged = stage / "PandaEditor";
#    if defined(PLATFORM_WINDOWS)
            const bool complete = std::filesystem::is_regular_file(staged / "PandaEditor.exe", error);
            // The helper runs from the updates folder: the install folder is the one it replaces.
            std::filesystem::copy_file(Fern::getStaticResourcesPath() / "Tools" / "PandaUpdateHelper.exe", updates / "PandaUpdateHelper.exe",
                                       std::filesystem::copy_options::overwrite_existing, error);
            if (error) {
                result.message = "The update helper could not be prepared: " + error.message();
                return result;
            }
#    else
            const bool complete = std::filesystem::is_regular_file(staged / "PandaEditor", error);
#    endif
            if (!complete) {
                result.message = "The archive holds no PandaEditor folder";
                return result;
            }
            std::filesystem::remove(archive, error);
            result.staged = staged.string();
            result.ok = true;
            return result;
#endif
        },
        [this, alive](InstallResult &&result) {
            if (!*alive) { return; }
            if (!result.ok) {
                fail(result.message);
                return;
            }
            PendingUpdate pending;
            pending.build = m_build;
            pending.version = m_version;
            pending.notes = m_notes;
            pending.staged = result.staged;
            if (!writeJson(pendingPath(), pending)) { LOG_WARN("Updater: the pending update could not be recorded"); }
            m_busy = false;
            m_request = Fern::Net::RequestHandle();
            LOG_INFO("Updater: %s (build %u) is installed; it runs after a restart", m_version.c_str(), m_build);
            setState(UpdateState::Kind::Ready, m_version, m_build, m_notes);
        }
    );
}

bool EditorUpdater::finishOnExit(bool relaunch) {
    const path_t pending = pendingPath();
    PendingUpdate update;
    if (pending.empty() || !readJson(pending, update)) { return false; }
    if (update.staged.empty() || update.build <= ENGINE_BUILD_NUMBER) { return false; }
    std::error_code error;
    const path_t staged = update.staged;
    if (!std::filesystem::is_directory(staged, error)) { return false; }
    const path_t install = installDirectory();
    if (install.empty()) { return false; }
#if defined(PLATFORM_WINDOWS)
    // The executable of this process cannot move while it runs: the helper waits for it.
    const path_t helper = staged.parent_path().parent_path() / "PandaUpdateHelper.exe";
    if (!std::filesystem::is_regular_file(helper, error)) {
        LOG_ERROR("Updater: the update helper is missing at %s", helper.string().c_str());
        return false;
    }
    std::vector<std::string> args = {
        "--wait", std::to_string(SystemTools::currentProcessId()), "--install", install.string(), "--staged", staged.string()
    };
    if (relaunch) { args.push_back("--launch"); }
    return SystemTools::startDetached(helper, args);
#else
    // The folder (the bundle) of a running executable may move on macOS and Linux: the swap
    // happens here, with the old folder as the rollback.
    path_t old = install;
    old += ".old";
    std::filesystem::remove_all(old, error);
    std::filesystem::rename(install, old, error);
    if (error) {
        LOG_ERROR("Updater: %s could not be moved aside: %s", install.string().c_str(), error.message().c_str());
        return false;
    }
    std::string message;
    if (!moveDirectory(staged, install, message)) {
        std::filesystem::rename(old, install, error);
        LOG_ERROR("Updater: the new build could not be moved into %s: %s", install.string().c_str(), message.c_str());
        return false;
    }
    std::filesystem::remove_all(old, error); // the next start removes it if this fails
    std::filesystem::remove_all(staged.parent_path(), error);
    if (relaunch) {
#    if defined(PLATFORM_MACOS)
        SystemTools::startDetached("/usr/bin/open", {"-n", install.string()});
#    else
        SystemTools::startDetached(install / "PandaEditor", {});
#    endif
    }
    return true;
#endif
}

} // namespace Panda
