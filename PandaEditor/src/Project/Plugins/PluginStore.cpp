#include "Project/Plugins/PluginStore.hpp"

#include "Project/Plugins/Semver.hpp"
#include "Project/System/Portal.hpp"
#include "Project/System/SystemTools.hpp"
#include "Project/System/ZipArchive.hpp"

#include <Foundation/Foundation.hpp>
#include <Foundation/Hash/Sha256.hpp>
#include <Foundation/PlatformDetection.hpp>
#include <Panda/Application/Application.hpp>
#include <Panda/Base/Version.hpp>
#include <Rain/Coders/JsonDecoder.hpp>
#include <Rain/Coders/JsonEncoder.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace Panda {

namespace {

    // {updates: [...]} of GET /packages/updates.
    struct UpdatesResponse final : public Rain::Codable {
        std::vector<PluginUpdate> updates;

        RAIN_FIELDS_BEGIN(UpdatesResponse)
        RAIN_FIELD(updates)
        RAIN_FIELDS_END
    };

    template<typename T>
    bool decodeJson(const std::string &json, T &out) {
        std::istringstream stream(json);
        Rain::JsonDecoder decoder;
        Rain::Decoder *decoderPtr = &decoder;
        return decoderPtr->decode(stream, out);
    }

    std::string urlEncode(std::string_view text) {
        static const char *hex = "0123456789ABCDEF";
        std::string out;
        for (unsigned char c : text) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                out += static_cast<char>(c);
            } else {
                out += '%';
                out += hex[c >> 4];
                out += hex[c & 15];
            }
        }
        return out;
    }

    std::string errorLine(const Fern::Net::Response &response) {
        if (response.error != Fern::Net::Error::None) { return Fern::Net::errorName(response.error); }
        return "status " + std::to_string(response.status);
    }

    bool isDataOnly(const std::vector<std::string> &parts) {
        return parts.size() == 1 && parts[0] == "data";
    }

    // Files an archive of data must not carry: a plugin is data, nothing in it ever runs.
    bool isExecutableName(const std::string &name) {
        static const char *extensions[] = {".exe", ".dll", ".so", ".dylib", ".bat", ".cmd", ".com", ".sh", ".command", ".msi", ".scr", ".ps1", ".app"};
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (const char *extension : extensions) {
            const size_t length = std::char_traits<char>::length(extension);
            if (lower.size() >= length && lower.compare(lower.size() - length, length, extension) == 0) { return true; }
        }
        return false;
    }

} // namespace

struct PluginStore::OperationResult {
    bool ok = false;
    std::string message;
};

PluginStore::PluginStore()
    : m_alive(std::make_shared<bool>(true)) {}

PluginStore::~PluginStore() {
    *m_alive = false;
    m_client.reset(); // cancels the requests in flight and waits for the stack
}

/* ------------------------------ Folders ------------------------------ */

path_t PluginStore::pluginsDirectory() const {
    return m_userDirectory.empty() ? path_t() : m_userDirectory / "Plugins";
}

path_t PluginStore::stagingDirectory() const {
    return m_userDirectory.empty() ? path_t() : pluginsDirectory() / ".staging";
}

path_t PluginStore::registryDirectory() const {
    return m_userDirectory.empty() ? path_t() : m_userDirectory / "registry";
}

std::string PluginStore::portalPageUrl(const std::string &id) {
    return std::string(Portal::kPluginsPage) + "/" + id;
}

std::string PluginStore::installedSignature(const std::vector<InstalledPlugin> &plugins) {
    std::vector<std::string> entries;
    for (const InstalledPlugin &plugin : plugins) {
        entries.push_back(plugin.manifest.id + "@" + plugin.manifest.version + (plugin.dev ? "+dev" : ""));
    }
    std::sort(entries.begin(), entries.end());
    std::string signature;
    for (const std::string &entry : entries) { signature += entry + "\n"; }
    return signature;
}

Fern::Net::Client &PluginStore::client() {
    if (!m_client) {
        Fern::Net::ClientDesc desc;
        desc.userAgent = std::string("PandaEngine/") + ENGINE_VERSION;
        m_client = std::make_unique<Fern::Net::Client>(Application::get()->getScheduler(), desc);
    }
    return *m_client;
}

/* ------------------------------ Installed plugins ------------------------------ */

void PluginStore::initialize(std::vector<path_t> builtInDirectories, const path_t &userDirectory, LoadImageFn loadImage) {
    m_builtInDirectories = std::move(builtInDirectories);
    m_userDirectory = userDirectory;
    m_loadImage = std::move(loadImage);
    if (!m_userDirectory.empty()) {
        // Leftovers of a download or an unpack that did not finish.
        std::error_code error;
        std::filesystem::remove_all(stagingDirectory(), error);
    }
    m_installed.scan(m_builtInDirectories, pluginsDirectory());
    m_installedSignature = installedSignature(installed());
    bump();
}

void PluginStore::start(bool autoUpdate) {
    m_autoUpdate = autoUpdate;
    m_started = true;
    checkUpdates();
}

void PluginStore::setAutoUpdate(bool on) {
    const bool turnedOn = on && !m_autoUpdate;
    m_autoUpdate = on;
    if (turnedOn && m_started) {
        std::vector<std::string> ids;
        for (const auto &[id, update] : m_updates) { ids.push_back(id); }
        for (const std::string &id : ids) { update(id); }
    }
}

void PluginStore::rescan() {
    m_installed.scan(m_builtInDirectories, pluginsDirectory());
    m_installedSignature = installedSignature(installed());
    bump();
    // A reload is asked for because the files changed: the host reapplies even when the set did not.
    if (m_onInstalledChanged) { m_onInstalledChanged(); }
}

void PluginStore::rescanAndNotify() {
    m_installed.scan(m_builtInDirectories, pluginsDirectory());
    // An update that is installed now, or whose plugin is gone, is no update any more.
    for (auto it = m_updates.begin(); it != m_updates.end();) {
        const InstalledPlugin *plugin = findInstalled(it->first);
        if (!plugin || plugin->dev || plugin->builtIn || semverCompare(it->second.version, plugin->manifest.version) <= 0) {
            it = m_updates.erase(it);
        } else {
            ++it;
        }
    }
    const std::string signature = installedSignature(installed());
    bump();
    if (signature != m_installedSignature) {
        m_installedSignature = signature;
        if (m_onInstalledChanged) { m_onInstalledChanged(); }
    }
}

/* ------------------------------ Catalog ------------------------------ */

void PluginStore::fetchCatalog(const CatalogQuery &query, bool refresh) {
    if (!refresh && query == m_catalog.query && (m_catalog.loading || !m_catalog.items.empty() || m_catalog.failed)) { return; }
    m_catalogRequest.cancel();
    ++m_catalogGeneration;
    m_catalog = CatalogState{};
    m_catalog.query = query;
    requestPage(1);
}

void PluginStore::fetchMore() {
    if (m_catalog.loading || m_catalog.failed || m_catalog.fromCache || !m_catalog.hasMore()) { return; }
    requestPage(m_catalog.nextPage);
}

void PluginStore::requestPage(int page) {
    m_catalog.loading = true;
    bump();
    std::string url = std::string(Portal::kApi) + "/packages?kind=plugin&sort=downloads&engine=" + ENGINE_VERSION + "&page=" +
                      std::to_string(page) + "&size=" + std::to_string(kPageSize);
    if (!m_catalog.query.search.empty()) { url += "&q=" + urlEncode(m_catalog.query.search); }
    if (!m_catalog.query.category.empty()) { url += "&category=" + urlEncode(m_catalog.query.category); }
    Fern::Net::Request request;
    request.url = url;
    request.timeouts.totalSeconds = 60;
    const uint64_t generation = m_catalogGeneration;
    std::shared_ptr<bool> alive = m_alive;
    m_catalogRequest = client().send(request, [this, alive, generation, page](Fern::Net::Response &&response) {
        if (!*alive) { return; }
        onCatalogPage(generation, page, std::move(response));
    });
}

void PluginStore::onCatalogPage(uint64_t generation, int page, Fern::Net::Response &&response) {
    if (generation != m_catalogGeneration) { return; } // an older query
    m_catalog.loading = false;
    CatalogPage data;
    const bool readable = response.ok() && decodeJson(response.bodyText(), data);
    if (!readable) {
        const std::string line = response.ok() ? "unreadable answer" : errorLine(response);
        if (page == 1) {
            if (m_catalog.query == CatalogQuery{} && loadCachedCatalog()) {
                m_catalog.fromCache = true;
                m_catalog.error = "The registry did not answer (" + line + "). This is the catalog saved last time.";
            } else {
                m_catalog.failed = true;
                m_catalog.error = "Failed to load plugins (" + line + "). Check your connection and try again.";
            }
        } else {
            m_catalog.error = "The next page did not load (" + line + ").";
        }
        LOG_INFO_EDITOR("Plugins: %s", m_catalog.error.c_str());
        bump();
        return;
    }
    if (page == 1) {
        m_catalog.items.clear();
        if (m_catalog.query == CatalogQuery{}) { saveCachedCatalog(data); }
    }
    for (CatalogPlugin &item : data.items) {
        const bool known = std::any_of(m_catalog.items.begin(), m_catalog.items.end(), [&](const CatalogPlugin &existing) {
            return existing.id == item.id;
        });
        if (!known) { m_catalog.items.push_back(std::move(item)); }
    }
    m_catalog.total = data.items.empty() ? static_cast<uint64_t>(m_catalog.items.size()) : std::max<uint64_t>(data.total, m_catalog.items.size());
    m_catalog.nextPage = page + 1;
    m_catalog.error.clear();
    bump();
}

bool PluginStore::loadCachedCatalog() {
    if (m_userDirectory.empty()) { return false; }
    std::ifstream file(registryDirectory() / "index.json");
    if (!file.is_open()) { return false; }
    std::stringstream buffer;
    buffer << file.rdbuf();
    CatalogPage data;
    if (!decodeJson(buffer.str(), data)) { return false; }
    m_catalog.items = std::move(data.items);
    m_catalog.total = m_catalog.items.size();
    m_catalog.nextPage = 2;
    return true;
}

void PluginStore::saveCachedCatalog(const CatalogPage &page) {
    if (m_userDirectory.empty()) { return; }
    // The raw answer of the portal is kept as it came: it is read back through the same type.
    std::error_code error;
    std::filesystem::create_directories(registryDirectory(), error);
    if (error) { return; }
    Rain::JsonEncoder encoder;
    Rain::Encoder *encoderPtr = &encoder;
    std::ofstream file(registryDirectory() / "index.json");
    if (file.is_open()) { encoderPtr->encode(file, page); }
}

/* ------------------------------ Updates ------------------------------ */

const PluginUpdate *PluginStore::updateFor(const std::string &id) const {
    const auto it = m_updates.find(id);
    return it == m_updates.end() ? nullptr : &it->second;
}

void PluginStore::checkUpdates() {
    if (m_checkingUpdates || m_userDirectory.empty()) { return; }
    std::string ids;
    for (const InstalledPlugin &plugin : installed()) {
        if (plugin.builtIn || plugin.dev) { continue; }
        if (!ids.empty()) { ids += ","; }
        ids += plugin.manifest.id;
    }
    if (ids.empty()) {
        if (!m_updates.empty()) {
            m_updates.clear();
            bump();
        }
        return;
    }
    m_checkingUpdates = true;
    Fern::Net::Request request;
    request.url = std::string(Portal::kApi) + "/packages/updates?ids=" + urlEncode(ids) + "&engine=" + ENGINE_VERSION;
    request.timeouts.totalSeconds = 60;
    std::shared_ptr<bool> alive = m_alive;
    client().send(request, [this, alive](Fern::Net::Response &&response) {
        if (!*alive) { return; }
        m_checkingUpdates = false;
        UpdatesResponse data;
        if (!response.ok() || !decodeJson(response.bodyText(), data)) {
            LOG_INFO_EDITOR("Plugins: the update check did not answer (%s)", errorLine(response).c_str());
            return;
        }
        m_updates.clear();
        for (const PluginUpdate &update : data.updates) {
            const InstalledPlugin *plugin = findInstalled(update.id);
            if (!plugin || plugin->builtIn || plugin->dev) { continue; }
            if (semverCompare(update.version, plugin->manifest.version) > 0) { m_updates[update.id] = update; }
        }
        bump();
        if (m_autoUpdate) {
            std::vector<std::string> ids;
            for (const auto &[id, update] : m_updates) { ids.push_back(id); }
            for (const std::string &id : ids) { this->update(id); }
        }
    });
}

/* ------------------------------ Operations ------------------------------ */

const PluginOperation *PluginStore::operationFor(const std::string &id) const {
    const auto it = m_operations.find(id);
    return it == m_operations.end() ? nullptr : &it->second;
}

const std::string *PluginStore::errorFor(const std::string &id) const {
    const auto it = m_errors.find(id);
    return it == m_errors.end() ? nullptr : &it->second;
}

void PluginStore::clearError(const std::string &id) {
    if (m_errors.erase(id) > 0) { bump(); }
}

void PluginStore::setNotice(const std::string &title, const std::string &message) {
    m_notice.title = title;
    m_notice.message = message;
    ++m_notice.revision;
    LOG_ERROR_EDITOR("Plugins: %s: %s", title.c_str(), message.c_str());
    bump();
}

void PluginStore::install(const std::string &id) {
    beginInstall(id, PluginOperation::Kind::Install);
}

void PluginStore::update(const std::string &id) {
    beginInstall(id, PluginOperation::Kind::Update);
}

void PluginStore::beginInstall(const std::string &id, PluginOperation::Kind kind) {
    if (m_operations.count(id) > 0) { return; }
    if (m_userDirectory.empty()) {
        m_errors[id] = "The user data folder is not available: nothing can be installed";
        bump();
        return;
    }
    if (const InstalledPlugin *plugin = findInstalled(id); plugin && (plugin->builtIn || plugin->dev)) {
        m_errors[id] = plugin->builtIn ? "A built-in plugin cannot be replaced" : "A dev plugin with this id is installed: remove it first";
        bump();
        return;
    }
    m_errors.erase(id);
    PluginOperation operation;
    operation.kind = kind;
    m_operations[id] = operation;
    bump();

    // The version: the latest one that works with this engine, with its size and hash.
    Fern::Net::Request request;
    request.url = std::string(Portal::kApi) + "/packages/updates?ids=" + urlEncode(id) + "&engine=" + ENGINE_VERSION;
    request.timeouts.totalSeconds = 60;
    std::shared_ptr<bool> alive = m_alive;
    m_downloads[id] = client().send(request, [this, alive, id](Fern::Net::Response &&response) {
        if (!*alive) { return; }
        UpdatesResponse data;
        if (!response.ok() || !decodeJson(response.bodyText(), data)) {
            finishOperation(id, false, "The registry did not answer (" + errorLine(response) + ")");
            return;
        }
        const PluginUpdate *version = nullptr;
        for (const PluginUpdate &candidate : data.updates) {
            if (candidate.id == id) { version = &candidate; }
        }
        if (!version || version->version.empty()) {
            finishOperation(id, false, std::string("No version of this plugin works with Panda ") + ENGINE_VERSION);
            return;
        }
        downloadAndInstall(id, *version);
    });
}

void PluginStore::downloadAndInstall(const std::string &id, const PluginUpdate &version) {
    const path_t staging = stagingDirectory();
    std::error_code error;
    std::filesystem::create_directories(staging, error);
    if (error) {
        finishOperation(id, false, "The staging folder could not be created: " + error.message());
        return;
    }
    const path_t archive = staging / (id + "-" + version.version + ".zip");
    std::filesystem::remove(archive, error);

    Fern::Net::Request request;
    request.url = std::string(Portal::kApi) + "/packages/" + urlEncode(id) + "/versions/" + urlEncode(version.version) + "/download?from=editor";
    request.downloadTo = archive;
    if (version.archiveSize > 0) { request.expectedSize = version.archiveSize; }
    if (std::optional<Foundation::Sha256Digest> digest = Foundation::Sha256Digest::fromHex(version.sha256)) { request.expectedSha256 = digest; }
    request.timeouts.totalSeconds = 600;

    if (auto it = m_operations.find(id); it != m_operations.end()) {
        it->second.version = version.version;
        it->second.progress = 0.f;
    }
    bump();

    std::shared_ptr<bool> alive = m_alive;
    const path_t plugins = pluginsDirectory();
    const std::string expectedVersion = version.version;
    m_downloads[id] = client().send(
        request,
        [this, alive, id, archive, plugins, expectedVersion](Fern::Net::Response &&response) {
            if (!*alive) { return; }
            if (!response.ok()) {
                std::error_code removeError;
                std::filesystem::remove(archive, removeError);
                finishOperation(id, false, "The download failed (" + errorLine(response) + ")");
                return;
            }
            // Unpacking, checking and swapping the folder happen off the main thread.
            const path_t unpack = archive.parent_path() / (id + "-" + expectedVersion);
            Application::get()->getScheduler().submit<OperationResult>(
                PandaAsync::JobDesc{"plugin install", PandaAsync::Priority::Background},
                [archive, unpack, plugins, id, expectedVersion](PandaAsync::Context &) -> OperationResult {
                    OperationResult result;
                    std::error_code error;
                    auto cleanup = [&] {
                        std::filesystem::remove_all(unpack, error);
                        std::filesystem::remove(archive, error);
                    };
                    std::string zipError;
                    const std::optional<std::vector<std::string>> entries = ZipArchive::list(archive, zipError);
                    if (!entries) {
                        result.message = "The archive cannot be read: " + zipError;
                        cleanup();
                        return result;
                    }
                    // One root folder, only data inside.
                    std::string root;
                    for (const std::string &entry : *entries) {
                        const size_t slash = entry.find('/');
                        const std::string first = slash == std::string::npos ? entry : entry.substr(0, slash);
                        if (first.empty() || first == "." || first == "..") {
                            result.message = "The archive has an entry outside its root folder: " + entry;
                            cleanup();
                            return result;
                        }
                        if (root.empty()) {
                            root = first;
                        } else if (root != first) {
                            result.message = "The archive must hold exactly one root folder";
                            cleanup();
                            return result;
                        }
                        if (isExecutableName(entry)) {
                            result.message = "The archive contains an executable file: " + entry;
                            cleanup();
                            return result;
                        }
                    }
                    if (root.empty()) {
                        result.message = "The archive is empty";
                        cleanup();
                        return result;
                    }
                    std::filesystem::remove_all(unpack, error);
                    if (!ZipArchive::extract(archive, unpack, zipError)) {
                        result.message = "The archive could not be unpacked: " + zipError;
                        cleanup();
                        return result;
                    }
                    const path_t folder = unpack / root;
                    PluginManifest manifest;
                    if (!EditorPlugins::readManifest(folder / "plugin.json", manifest) || manifest.id.empty()) {
                        result.message = "The archive has no readable plugin.json";
                        cleanup();
                        return result;
                    }
                    if (manifest.manifestVersion != 1) {
                        result.message = "plugin.json has manifest version " + std::to_string(manifest.manifestVersion) + "; this editor reads version 1";
                    } else if (manifest.id != id) {
                        result.message = "The archive holds " + manifest.id + ", not " + id;
                    } else if (manifest.version != expectedVersion) {
                        result.message = "The archive holds version " + manifest.version + ", not " + expectedVersion;
                    } else if (!isDataOnly(manifest.parts)) {
                        result.message = "The plugin is not a data plugin (parts must be [\"data\"])";
                    } else if (!semverSatisfies(ENGINE_VERSION, manifest.engine)) {
                        result.message = "The plugin needs Panda " + manifest.engine + "; this editor is " + ENGINE_VERSION;
                    }
                    if (!result.message.empty()) {
                        cleanup();
                        return result;
                    }
                    // The swap: the old folder goes, the unpacked one takes its place.
                    const path_t target = plugins / id;
                    std::filesystem::create_directories(plugins, error);
                    if (std::filesystem::is_symlink(target, error)) {
                        result.message = "A dev plugin with this id is installed: remove it first";
                        cleanup();
                        return result;
                    }
                    if (std::filesystem::exists(target, error)) {
                        std::filesystem::remove_all(target, error);
                        if (error) {
                            result.message = "The installed version could not be removed: " + error.message();
                            cleanup();
                            return result;
                        }
                    }
                    std::filesystem::rename(folder, target, error);
                    if (error) {
                        error.clear();
                        std::filesystem::copy(folder, target, std::filesystem::copy_options::recursive, error);
                        if (error) {
                            result.message = "The plugin folder could not be moved into place: " + error.message();
                            std::filesystem::remove_all(target, error);
                            cleanup();
                            return result;
                        }
                    }
                    cleanup();
                    result.ok = true;
                    return result;
                },
                [this, alive, id](OperationResult &&result) {
                    if (!*alive) { return; }
                    finishOperation(id, result.ok, result.message);
                }
            );
        },
        [this, alive, id](const Fern::Net::Progress &progress) {
            if (!*alive) { return; }
            auto it = m_operations.find(id);
            if (it == m_operations.end()) { return; }
            it->second.progress = progress.total && *progress.total > 0 ? static_cast<float>(progress.received) / static_cast<float>(*progress.total) : -1.f;
            bump();
        }
    );
}

void PluginStore::remove(const std::string &id) {
    if (m_operations.count(id) > 0) { return; }
    const InstalledPlugin *plugin = findInstalled(id);
    if (!plugin) { return; }
    if (plugin->builtIn) {
        m_errors[id] = "A built-in plugin cannot be removed";
        bump();
        return;
    }
    m_errors.erase(id);
    PluginOperation operation;
    operation.kind = PluginOperation::Kind::Remove;
    m_operations[id] = operation;
    bump();
    const path_t target = pluginsDirectory() / id;
    const path_t marker = pluginsDirectory() / ".dev" / id;
    const bool dev = plugin->dev;
    std::shared_ptr<bool> alive = m_alive;
    Application::get()->getScheduler().submit<OperationResult>(
        PandaAsync::JobDesc{"plugin remove", PandaAsync::Priority::Background},
        [target, marker, dev](PandaAsync::Context &) -> OperationResult {
            OperationResult result;
            std::error_code error;
            if (dev || std::filesystem::is_symlink(target, error)) {
                // A dev plugin is a link: the developer's folder stays where it is.
                std::filesystem::remove(target, error);
                std::error_code markerError;
                std::filesystem::remove(marker, markerError);
            } else {
                std::filesystem::remove_all(target, error);
            }
            if (error) {
                result.message = "The plugin folder could not be removed: " + error.message();
                return result;
            }
            result.ok = true;
            return result;
        },
        [this, alive, id](OperationResult &&result) {
            if (!*alive) { return; }
            finishOperation(id, result.ok, result.message);
        }
    );
}

void PluginStore::installDev(const path_t &folder) {
    if (m_userDirectory.empty()) {
        setNotice("Dev plugin", "The user data folder is not available: nothing can be installed.");
        return;
    }
    PluginManifest manifest;
    if (!EditorPlugins::readManifest(folder / "plugin.json", manifest) || manifest.id.empty()) {
        setNotice("Not a plugin folder", folder.string() + " has no plugin.json with an id. A plugin folder holds plugin.json and data/.");
        return;
    }
    if (!isDataOnly(manifest.parts)) {
        setNotice("Not a data plugin", manifest.id + " must declare parts [\"data\"]: a plugin of the editor carries data, not code.");
        return;
    }
    if (!semverSatisfies(ENGINE_VERSION, manifest.engine)) {
        setNotice("Engine version", manifest.id + " declares engine \"" + manifest.engine + "\", which does not include this editor (" + ENGINE_VERSION + ").");
        return;
    }
    if (const InstalledPlugin *existing = findInstalled(manifest.id); existing && existing->builtIn) {
        setNotice("Built-in plugin", manifest.id + " is built into the editor and cannot be replaced.");
        return;
    }
    if (m_operations.count(manifest.id) > 0) {
        setNotice("Busy", manifest.id + " is being installed or removed right now.");
        return;
    }
    const path_t plugins = pluginsDirectory();
    const path_t markers = plugins / ".dev";
    const path_t target = plugins / manifest.id;
    const path_t marker = markers / manifest.id;
    std::error_code error;
    std::filesystem::create_directories(markers, error);
    if (error) {
        setNotice("Dev plugin", "The plugins folder could not be created: " + error.message());
        return;
    }
    // The developer's folder takes the place of whatever has this id: a link, or a registry copy.
    if (std::filesystem::is_symlink(target, error) || std::filesystem::exists(marker, error)) {
        std::filesystem::remove(target, error);
        std::filesystem::remove(marker, error);
    } else if (std::filesystem::exists(target, error)) {
        std::filesystem::remove_all(target, error);
    }
    if (error) {
        setNotice("Dev plugin", "The installed plugin could not be replaced: " + error.message());
        return;
    }
    const path_t source = std::filesystem::absolute(folder, error);
    error.clear();
    std::filesystem::create_directory_symlink(source, target, error);
    if (error) {
#if defined(PLATFORM_WINDOWS)
        // Without developer mode a symbolic link needs a privilege a junction does not.
        const char *comspec = std::getenv("ComSpec");
        const bool linked = SystemTools::runCommand(comspec ? path_t(comspec) : path_t("cmd.exe"), {"/c", "mklink", "/J", target.string(), source.string()});
        std::error_code check;
        if (!linked || !std::filesystem::is_directory(target, check)) {
            setNotice("Dev plugin", "The link " + target.string() + " could not be created: " + error.message());
            return;
        }
#else
        setNotice("Dev plugin", "The link " + target.string() + " could not be created: " + error.message());
        return;
#endif
    }
    // The marker names the source and tells a junction from a folder on Windows.
    std::ofstream markerFile(marker);
    if (markerFile.is_open()) { markerFile << source.generic_string(); }
    LOG_INFO_EDITOR("Plugins: dev plugin %s linked to %s", manifest.id.c_str(), source.string().c_str());
    rescan();
}

void PluginStore::finishOperation(const std::string &id, bool ok, const std::string &message) {
    m_operations.erase(id);
    m_downloads.erase(id);
    if (ok) {
        m_errors.erase(id);
        LOG_INFO_EDITOR("Plugins: %s done", id.c_str());
    } else {
        m_errors[id] = message;
        LOG_ERROR_EDITOR("Plugins: %s: %s", id.c_str(), message.c_str());
    }
    rescanAndNotify();
}

/* ------------------------------ Icons ------------------------------ */

PandaUI::TextureHandle PluginStore::icon(const std::string &id, const std::string &iconUrl, const path_t &localFile) {
    if (auto it = m_icons.find(id); it != m_icons.end()) {
        // Done, in flight, or failed for this address: an address that appeared later is tried.
        if (it->second.texture || it->second.loading || iconUrl.empty() || it->second.url == iconUrl) { return it->second.texture; }
    }
    IconEntry &entry = m_icons[id];
    entry.url = iconUrl;
    entry.failed = false;
    if (!m_loadImage) {
        entry.failed = true;
        return {};
    }
    std::error_code error;
    const path_t cached = m_userDirectory.empty() ? path_t() : registryDirectory() / "icons" / (id + ".png");
    if (!cached.empty() && !iconUrl.empty() && std::filesystem::is_regular_file(cached, error)) {
        entry.texture = m_loadImage(cached);
        if (entry.texture) { return entry.texture; }
    }
    if (!localFile.empty() && std::filesystem::is_regular_file(localFile, error)) {
        entry.texture = m_loadImage(localFile);
        if (entry.texture) { return entry.texture; }
    }
    if (iconUrl.empty() || cached.empty()) {
        entry.failed = true;
        return {};
    }
    std::filesystem::create_directories(cached.parent_path(), error);
    entry.loading = true;
    Fern::Net::Request request;
    request.url = iconUrl;
    request.downloadTo = cached;
    request.timeouts.totalSeconds = 60;
    std::shared_ptr<bool> alive = m_alive;
    client().send(request, [this, alive, id, cached](Fern::Net::Response &&response) {
        if (!*alive) { return; }
        IconEntry &loaded = m_icons[id];
        loaded.loading = false;
        if (response.ok()) { loaded.texture = m_loadImage(cached); }
        loaded.failed = !loaded.texture;
        bump();
    });
    return {};
}

} // namespace Panda
