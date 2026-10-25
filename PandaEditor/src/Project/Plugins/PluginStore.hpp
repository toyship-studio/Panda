#pragma once
// The plugin store (context/infrastructure.md, section 3): the installed plugins of the editor, the
// catalog of the portal, and the only code that changes the plugin folders. The Plugins tab is a
// passive view over it: it reads the state every frame (revision() grows with every change) and
// asks for operations. The network goes through Fern::Net, the file work runs on PandaAsync
// workers, and every result is applied on the main thread.
//
// Folders: built-in plugins come from the editor resources (Resources/plugins/*, never removed),
// installed ones live in <userdata>/Panda/Plugins/<id> (the folder name is the id, plugin.json is
// the truth), downloads and unpacking happen in <userdata>/Panda/Plugins/.staging, a dev plugin is
// a link <userdata>/Panda/Plugins/<id> to the developer's folder. The catalog is cached in
// <userdata>/Panda/registry/index.json and the icons in <userdata>/Panda/registry/icons.
#include "Project/Plugins/EditorPlugins.hpp"

#include <Fern/Net/Http.hpp>
#include <Panda/Assets/Base/Path.hpp>
#include <PandaUI/Core/TextureHandle.hpp>
#include <Rain/Rain.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Panda {

/* ------------------------------ The catalog of the portal ------------------------------ */

struct CatalogOwner final : public Rain::Codable {
    std::string id;
    std::string name;
    std::string githubLogin;

    RAIN_FIELDS_BEGIN(CatalogOwner)
    RAIN_FIELD(id)
    RAIN_FIELD(name)
    RAIN_FIELD(githubLogin)
    RAIN_FIELDS_END
};

// PackageSummary of the API (panda-portal/docs/api.md).
struct CatalogPlugin final : public Rain::Codable {
    std::string id;
    std::string kind;
    std::string name;
    std::string description;
    std::string category;
    std::vector<std::string> tags;
    std::vector<std::string> provides;
    std::string license;
    CatalogOwner owner;
    std::string iconUrl;
    std::string latestVersion;
    uint64_t downloadsTotal = 0;
    std::string updatedAt;
    bool unlisted = false;

    RAIN_FIELDS_BEGIN(CatalogPlugin)
    RAIN_FIELD(id)
    RAIN_FIELD(kind)
    RAIN_FIELD(name)
    RAIN_FIELD(description)
    RAIN_FIELD(category)
    RAIN_FIELD(tags)
    RAIN_FIELD(provides)
    RAIN_FIELD(license)
    RAIN_FIELD(owner)
    RAIN_FIELD(iconUrl)
    RAIN_FIELD(latestVersion)
    RAIN_FIELD(downloadsTotal)
    RAIN_FIELD(updatedAt)
    RAIN_FIELD(unlisted)
    RAIN_FIELDS_END
};

struct CatalogPage final : public Rain::Codable {
    std::vector<CatalogPlugin> items;
    uint64_t total = 0;
    int page = 1;
    int size = 0;

    RAIN_FIELDS_BEGIN(CatalogPage)
    RAIN_FIELD(items)
    RAIN_FIELD(total)
    RAIN_FIELD(page)
    RAIN_FIELD(size)
    RAIN_FIELDS_END
};

// What the tab asks the catalog for; the portal filters, the editor only pages.
struct CatalogQuery {
    std::string search;   // q
    std::string category; // one category, empty — all

    bool operator==(const CatalogQuery &other) const = default;
};

struct CatalogState {
    CatalogQuery query;
    std::vector<CatalogPlugin> items; // the pages loaded so far
    uint64_t total = 0;               // on the portal, for this query
    int nextPage = 1;
    bool loading = false;             // a page is in flight
    bool failed = false;              // the first page did not come and there is nothing cached
    bool fromCache = false;           // the portal did not answer: the items are the saved index
    std::string error;                // one line when failed or fromCache

    bool hasMore() const {
        return items.size() < total;
    }
};

// The latest version of a plugin that works with this engine (GET /packages/updates).
struct PluginUpdate final : public Rain::Codable {
    std::string id;
    std::string version;
    uint64_t archiveSize = 0;
    std::string sha256;
    std::string engineRange;

    RAIN_FIELDS_BEGIN(PluginUpdate)
    RAIN_FIELD(id)
    RAIN_FIELD(version)
    RAIN_FIELD(archiveSize)
    RAIN_FIELD(sha256)
    RAIN_FIELD(engineRange)
    RAIN_FIELDS_END
};

/* ------------------------------ Operations ------------------------------ */

struct PluginOperation {
    enum class Kind { Install, Update, Remove };
    Kind kind = Kind::Install;
    std::string version; // Install and Update: the version in flight
    float progress = -1.f; // the download, 0..1; -1 unknown or not downloading
};

// A message for the tab that belongs to no plugin card (a dev install that failed).
struct PluginNotice {
    std::string title;
    std::string message;
    uint64_t revision = 0; // grows with every new notice
};

class PluginStore final {
public:
    PluginStore();
    ~PluginStore();
    PluginStore(const PluginStore &) = delete;
    PluginStore &operator=(const PluginStore &) = delete;

    // Textures are created by the UI host (EditorUI::loadImage): the store only knows the files.
    using LoadImageFn = std::function<PandaUI::TextureHandle(const path_t &)>;

    // The folders and the scan. builtInDirectories: the plugin folders of the editor resources;
    // userDirectory: <userdata>/Panda — the store uses Plugins/ and registry/ inside it (empty — no
    // user data: built-in plugins only, nothing can be installed).
    void initialize(std::vector<path_t> builtInDirectories, const path_t &userDirectory, LoadImageFn loadImage);
    // After the settings are read: an update check of the installed plugins when there are any.
    void start(bool autoUpdate);
    void setAutoUpdate(bool on);

    /* ------------------------------ Installed plugins ------------------------------ */

    const std::vector<InstalledPlugin> &installed() const {
        return m_installed.plugins();
    }
    const InstalledPlugin *findInstalled(const std::string &id) const {
        return m_installed.find(id);
    }
    const std::vector<EditorTheme> &themes() const {
        return m_installed.themes();
    }
    const EditorTheme *findTheme(const std::string &id) const {
        return m_installed.findTheme(id);
    }
    // Rereads the folders (the Reload of a dev plugin); tells the host when the set changed.
    void rescan();
    // The set of installed plugins changed (an install, an update, a removal, a rescan with a
    // difference): the host reapplies the data it uses (the theme).
    void setOnInstalledChanged(std::function<void()> handler) {
        m_onInstalledChanged = std::move(handler);
    }

    /* ------------------------------ Catalog ------------------------------ */

    // The first page of a query (a changed query drops the loaded pages); the same query again does
    // nothing unless refresh is set.
    void fetchCatalog(const CatalogQuery &query, bool refresh = false);
    void fetchMore(); // the next page of the current query
    const CatalogState &catalog() const {
        return m_catalog;
    }
    static constexpr int kPageSize = 20;

    /* ------------------------------ Updates ------------------------------ */

    // Asks the portal for the latest compatible version of every installed registry plugin; with
    // automatic updates on, the newer ones are installed at once.
    void checkUpdates();
    const std::unordered_map<std::string, PluginUpdate> &updates() const {
        return m_updates;
    }
    const PluginUpdate *updateFor(const std::string &id) const;

    /* ------------------------------ Operations ------------------------------ */

    void install(const std::string &id);
    void update(const std::string &id);
    void remove(const std::string &id);
    // Links the developer's folder as Plugins/<id> (its plugin.json names the id); a plugin with that
    // id that came from the registry is replaced. Problems come back as a notice.
    void installDev(const path_t &folder);

    const PluginOperation *operationFor(const std::string &id) const;
    // The message of the last failed operation of a plugin, until the next attempt.
    const std::string *errorFor(const std::string &id) const;
    void clearError(const std::string &id);
    const PluginNotice &notice() const {
        return m_notice;
    }

    /* ------------------------------ Icons ------------------------------ */

    // The icon of a plugin: the cached file, or the download of iconUrl into the cache (an invalid
    // handle until it arrives; the revision grows when it does). localFile: the icon.png of an
    // installed plugin, used when there is no address.
    PandaUI::TextureHandle icon(const std::string &id, const std::string &iconUrl, const path_t &localFile);

    // The page of the plugin on the portal.
    static std::string portalPageUrl(const std::string &id);

    // Grows with every change of any state above: the tab compares and rebuilds.
    uint64_t revision() const {
        return m_revision;
    }

private:
    struct OperationResult;
    struct IconEntry {
        PandaUI::TextureHandle texture;
        std::string url; // the address tried; a new one is tried again
        bool loading = false;
        bool failed = false;
    };

    void bump() {
        ++m_revision;
    }
    void requestPage(int page);
    void onCatalogPage(uint64_t generation, int page, Fern::Net::Response &&response);
    bool loadCachedCatalog();
    void saveCachedCatalog(const CatalogPage &page);
    void beginInstall(const std::string &id, PluginOperation::Kind kind);
    void downloadAndInstall(const std::string &id, const PluginUpdate &version);
    void runRemove(const std::string &id);
    void finishOperation(const std::string &id, bool ok, const std::string &message);
    void rescanAndNotify();
    void setNotice(const std::string &title, const std::string &message);
    Fern::Net::Client &client();
    path_t pluginsDirectory() const;
    path_t stagingDirectory() const;
    path_t registryDirectory() const;
    static std::string installedSignature(const std::vector<InstalledPlugin> &plugins);

    std::vector<path_t> m_builtInDirectories;
    path_t m_userDirectory;
    LoadImageFn m_loadImage;
    EditorPlugins m_installed;
    std::string m_installedSignature; // ids and versions, to tell a real change from a rescan
    std::function<void()> m_onInstalledChanged;

    std::unique_ptr<Fern::Net::Client> m_client;
    std::shared_ptr<bool> m_alive; // completions outlive nothing: they check it

    CatalogState m_catalog;
    uint64_t m_catalogGeneration = 0; // answers of an older query are dropped
    Fern::Net::RequestHandle m_catalogRequest;

    std::unordered_map<std::string, PluginUpdate> m_updates;
    bool m_checkingUpdates = false;
    bool m_autoUpdate = true;
    bool m_started = false;

    std::unordered_map<std::string, PluginOperation> m_operations;
    std::unordered_map<std::string, Fern::Net::RequestHandle> m_downloads;
    std::unordered_map<std::string, std::string> m_errors;
    PluginNotice m_notice;

    std::unordered_map<std::string, IconEntry> m_icons;
    uint64_t m_revision = 1;
};

} // namespace Panda
