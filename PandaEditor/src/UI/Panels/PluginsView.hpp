#pragma once
#include "UI/Documents/PluginsDocument.hpp"

#include <Foundation/RevisionWatcher.hpp>
#include <PandaEditorKit/Widgets/EditorView.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Editor {

// Everything a card shows and the state its action follows. Built by the view from the store and
// compared: a card is rebuilt only when its model changed.
struct PluginCardModel {
    enum class Status {
        NotInstalled,
        Installing,
        Installed,       // the installed version is the latest compatible one
        UpdateAvailable, // a newer compatible version exists on the portal
        Updating,
        Removing,
        BuiltIn,         // shipped with the editor: nothing to install or remove
        Dev,             // a link to the developer's folder: Reload and Uninstall
        OverriddenByDev, // a registry card whose id is installed as a dev plugin
    };

    std::string id;
    std::string name;
    std::string version;          // the latest version on the portal, or the installed one
    std::string installedVersion; // empty when not installed
    std::string description;
    std::string authors;          // "A, B" of the manifest, or the owner of the portal
    std::string category;
    std::string license;
    std::string engineRange;      // the installed manifest's engine range (installed cards)
    std::string updatedAt;        // "2026-10-24" (catalog cards)
    std::string portalUrl;        // the page on the portal (catalog cards and registry installs)
    std::string repositoryUrl;    // links.repository of the installed manifest
    std::vector<std::string> tags;
    std::vector<std::string> provides; // "themes", ...
    uint64_t downloads = 0;
    bool fromCatalog = false;          // downloads and updatedAt mean something
    Status status = Status::NotInstalled;
    float progress = -1.f;            // Installing / Updating: the download, 0..1; -1 unknown
    std::string error;                // the last failed operation
    std::vector<std::pair<std::string, std::string>> themes; // {theme id, name} of an installed plugin
    std::string activeTheme;          // the theme id in use
    PandaUI::TextureHandle icon;

    bool operator==(const PluginCardModel &other) const = default;
};

// The actions of a card, routed back to the view.
struct PluginCardActions {
    std::function<void(const std::string &id)> install;
    std::function<void(const std::string &id)> update;
    std::function<void(const std::string &id)> uninstall;
    std::function<void(const std::string &id)> reload;
    std::function<void(const std::string &id)> retry; // after an error: clears it
    std::function<void(const std::string &themeId)> selectTheme;
    std::function<void(const std::string &url)> openUrl;
};

class PluginCard final : public PandaUI::Panel {
public:
    explicit PluginCard(const PluginCardActions *actions);
    void setModel(const PluginCardModel &model);
    const PluginCardModel &model() const {
        return m_model;
    }
    void update(double deltaTime) override;

private:
    void rebuild();

    const PluginCardActions *m_actions;
    PluginCardModel m_model;
    bool m_built = false;
    bool m_needsRebuild = false; // the subviews are rebuilt once per frame, however many models arrived
};

// VIEW: the Plugins tab (Documents/PluginsDocument). The Extensions page of Zed: a title with
// Install Dev Plugin, the search field with the All / Installed / Not Installed filter, the
// category chips, the list of cards, and an empty state. Reads the store every frame (its
// revision) and rebuilds what changed; the catalog pages are loaded as the list is scrolled.
class PluginsView final : public EditorView {
public:
    explicit PluginsView(PluginsHost host);
    void refresh();
    void update(double deltaTime) override;

private:
    enum class Filter { All, Installed, NotInstalled };
    static constexpr double kSearchDebounceSeconds = 0.3;

    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;
    void requestCatalog();
    void rebuildChips();
    void rebuildList();
    std::vector<PluginCardModel> buildModels() const;
    std::string emptyMessage() const;
    std::shared_ptr<PandaUI::Button> makeChip(const std::string &title, bool selected, std::function<void()> onClick);

    PluginsHost m_host;
    PluginCardActions m_actions;
    Filter m_filter = Filter::All;
    std::string m_category; // empty — all
    std::string m_search;   // the query sent to the store
    bool m_searchPending = false;
    double m_searchTimer = 0.0;
    bool m_chipsDirty = false; // the chips and the list are rebuilt once per frame, after every change
    bool m_listDirty = false;  // of the frame was applied
    Foundation::RevisionWatcher m_storeWatcher;  // the store changed since the last rebuild
    Foundation::RevisionWatcher m_noticeWatcher; // a new notice to show

    std::shared_ptr<PandaUI::Label> m_title;
    std::shared_ptr<PandaUI::Button> m_devButton;
    std::shared_ptr<PandaUI::TextField> m_searchField;
    std::shared_ptr<PandaUI::SegmentedControl> m_filterControl;
    std::shared_ptr<PandaUI::Panel> m_chips;
    std::shared_ptr<PandaUI::Label> m_notice;    // the cached-catalog line
    std::shared_ptr<PandaUI::ScrollView> m_list;
    std::shared_ptr<PandaUI::Panel> m_cards;
    std::shared_ptr<PandaUI::Label> m_empty;
    std::shared_ptr<PandaUI::Panel> m_footer;    // "Showing N of M" + Load more
    std::shared_ptr<PandaUI::Label> m_footerText;
    std::shared_ptr<PandaUI::Button> m_loadMore;
    std::shared_ptr<PandaUI::Button> m_retry;
    std::unordered_map<std::string, std::shared_ptr<PluginCard>> m_cardViews; // by plugin id
};

} // namespace Editor
