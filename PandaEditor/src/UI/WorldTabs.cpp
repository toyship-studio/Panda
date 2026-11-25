#include "UI/WorldTabs.hpp"

#include "App/EditorLayer.hpp"
#include "Project/Documents/WorldFile.hpp"

#include <PandaEditorKit/EditorHost.hpp>
#include <PandaEditorKit/Widgets/Icons.hpp>
#include <PandaUI/PandaUI.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace Editor {

using namespace PandaUI;

namespace {

    // The tab of an open world: world:<id of the open world>. The ids live for one run, so a saved
    // layout only remembers that a world was in front of the center (LAYOUT_MARKER).
    constexpr const char *TAB_PREFIX = "world:";
    constexpr const char *LAYOUT_MARKER = "world";

    std::string tabIdOf(uint32_t worldId) {
        return TAB_PREFIX + std::to_string(worldId);
    }
    uint32_t worldIdOfTab(const std::string &tabId) {
        return static_cast<uint32_t>(std::strtoul(tabId.c_str() + std::strlen(TAB_PREFIX), nullptr, 10));
    }

} // namespace

// A world document: the tab of one open world — a view of its file document (WorldFile, found by
// the id the tab was opened with: the file may close before its tab does). Dirty is the world's
// change flag, Save writes the world file, the undo stack is the world's. The tab content is a
// host panel: what a world tab shows (WorldEditorView) lives in the host of the active world
// (WorldTabs::sync), the other hosts are empty and keep what they showed of their world (kept).
class WorldDocument final : public Document {
public:
    WorldDocument(WorldTabs &tabs, uint32_t worldId)
        : m_tabs(tabs)
        , m_worldId(worldId)
        , m_host(std::make_shared<Panel>()) {
        m_host->setBackgroundColor(ThemeColor::windowBackground);
        m_host->layout().setFlexDirection(FlexDirection::Column);
        m_host->layout().setFlexGrow(1.f);
    }

    uint32_t worldId() const {
        return m_worldId;
    }
    const std::shared_ptr<Panel> &host() const {
        return m_host;
    }
    WorldTabState kept; // what the tab showed of its world when another world came in front

    // The name of the file the tab edits, as every document tab shows it.
    std::string title() override {
        const Panda::WorldFile *file = m_tabs.worldFile(m_worldId);
        return file ? file->name() : "World";
    }
    std::string icon() override {
        // The file the tab edits: the icon it has in the Content Browser.
        const Panda::WorldFile *file = m_tabs.worldFile(m_worldId);
        return file != nullptr && file->isPrefab() ? Ic::Prefab : Ic::AssetWorld;
    }
    std::shared_ptr<PandaUI::View> view() override {
        return m_host;
    }
    bool isDirty() override {
        const Panda::WorldFile *file = m_tabs.worldFile(m_worldId);
        return file && file->isDirty();
    }
    bool save() override {
        Panda::WorldFile *file = m_tabs.worldFile(m_worldId);
        return file && m_tabs.m_app.documentActions().saveWorld(*file);
    }
    std::filesystem::path filePath() override {
        const Panda::WorldFile *file = m_tabs.worldFile(m_worldId);
        return file ? file->path() : std::filesystem::path();
    }
    Panda::CommandHistory *undoStack() override {
        // The active world is what the panels show: in Play that is the playing copy, and its
        // edits go away with it.
        if (isActive()) {
            Panda::World *shown = m_tabs.m_app.panelsWorld();
            return shown ? &shown->getCommandHistory() : nullptr;
        }
        Panda::WorldFile *file = m_tabs.worldFile(m_worldId);
        return file ? &file->world().getCommandHistory() : nullptr;
    }
    void refresh() override {
        if (!isActive()) { return; } // the panels show the active world
        if (Panda::World *shown = m_tabs.m_app.panelsWorld()) { shown->setChanged(); }
        editorHost().refreshFromWorld();
    }

private:
    bool isActive() const {
        return m_tabs.activeWorldId() == m_worldId;
    }

    WorldTabs &m_tabs;
    uint32_t m_worldId;
    std::shared_ptr<Panel> m_host;
};

bool WorldTabs::isWorldTab(const std::string &tabId) {
    return tabId.rfind(TAB_PREFIX, 0) == 0;
}

WorldTabs::WorldTabs(
    Panda::EditorLayer &app, DockManager &docks, EditorTabs &tabs, std::shared_ptr<PandaUI::View> content
)
    : m_app(app)
    , m_docks(docks)
    , m_tabs(tabs)
    , m_content(std::move(content)) {}

Panda::WorldFile *WorldTabs::worldFile(uint32_t worldId) const {
    return m_app.documents().findWorld(worldId);
}

uint32_t WorldTabs::activeWorldId() const {
    const Panda::WorldFile *active = m_app.documents().activeWorld();
    return active != nullptr ? active->id() : 0;
}

WorldDocument *WorldTabs::document(uint32_t worldId) {
    // Only these tabs have the world prefix, and only world documents are opened in them.
    return static_cast<WorldDocument *>(m_tabs.documentForTab(tabIdOf(worldId)));
}

Document *WorldTabs::activeDocument() {
    return document(activeWorldId());
}

void WorldTabs::showActive() {
    m_docks.showPanel(tabIdOf(activeWorldId()));
}

void WorldTabs::keepActiveState(WorldTabState state) {
    if (WorldDocument *active = document(activeWorldId())) { active->kept = std::move(state); }
}

WorldTabState WorldTabs::takeActiveState() {
    WorldDocument *active = document(activeWorldId());
    return active != nullptr ? std::exchange(active->kept, WorldTabState{}) : WorldTabState{};
}

void WorldTabs::sync() {
    if (m_inSync) { return; }
    m_inSync = true;
    // The worlds whose tabs the user closed leave the list of the project (their documents have
    // asked about the edits). The tab the user landed on says which world is shown next.
    if (!m_closedByUser.empty()) {
        const std::vector<uint32_t> closed = std::move(m_closedByUser);
        m_closedByUser.clear();
        for (uint32_t worldId : closed) {
            Panda::WorldFile *world = worldFile(worldId);
            if (world == nullptr) { continue; }
            if (worldId == activeWorldId() && !m_app.isPlaying()) {
                const std::string selected = m_docks.selectedPanelId(Dock::Center);
                Panda::WorldFile *next = isWorldTab(selected) ? worldFile(worldIdOfTab(selected)) : nullptr;
                if (next != nullptr) { m_app.activateWorld(*next); }
            }
            m_app.closeWorld(*world);
        }
    }
    // The tabs follow the list: the tab of a world that is gone closes, a new world gets a tab.
    m_closingTab = true;
    for (size_t i = 0; i < m_worlds.size();) {
        if (worldFile(m_worlds[i]) != nullptr) {
            ++i;
            continue;
        }
        m_tabs.closeTab(tabIdOf(m_worlds[i]));
        m_worlds.erase(m_worlds.begin() + static_cast<std::ptrdiff_t>(i));
    }
    m_closingTab = false;
    bool opened = false;
    for (const std::unique_ptr<Panda::WorldFile> &world : m_app.documents().worlds()) {
        const std::string tabId = tabIdOf(world->id());
        if (std::find(m_worlds.begin(), m_worlds.end(), world->id()) == m_worlds.end()) {
            m_worlds.push_back(world->id());
            m_tabs.openDocument(tabId, std::make_unique<WorldDocument>(*this, world->id()), Dock::Center);
            opened = true;
        }
        m_docks.setPanelTitle(tabId, world->name()); // a no-op while the name stands (Save As changes it)
    }
    // The active world and the selected tab follow each other.
    const uint32_t activeId = activeWorldId();
    const std::string activeTab = tabIdOf(activeId);
    if (activeId != m_shownWorldId || opened) {
        // The active world changed outside the tabs (a world was opened, created or closed): its
        // tab comes forward. A tab that has just been added selected itself — the active one stays.
        m_shownWorldId = activeId;
        m_docks.activateTransientPanel(activeTab);
    } else {
        const std::string selected = m_docks.selectedPanelId(Dock::Center);
        if (isWorldTab(selected) && selected != activeTab) {
            Panda::WorldFile *world = worldFile(worldIdOfTab(selected));
            if (!m_app.isPlaying() && world != nullptr) {
                m_app.activateWorld(*world);
                m_shownWorldId = activeWorldId();
            } else {
                // The game plays a copy of the active world: the tabs do not switch until it stops.
                m_docks.activateTransientPanel(activeTab);
                LOG_WARN_EDITOR("Stop the game to switch to another world");
            }
        }
    }
    // What a world tab shows lives in the tab of the active world.
    WorldDocument *active = document(activeWorldId());
    if (active != nullptr && m_content->getSuperview() != active->host().get()) {
        Context *context = m_docks.rootView()->getContext();
        View *focused = context ? context->getFocusedView() : nullptr; // a re-parent drops the focus
        active->host()->addSubview(m_content);
        if (context && focused) { context->setFocusedView(focused); }
    }
    m_inSync = false;
}

void WorldTabs::open(Panda::AssetId asset) {
    m_app.openWorldAsset(asset);
    sync();
}

void WorldTabs::documentClosed(Document *document) {
    auto *world = dynamic_cast<WorldDocument *>(document);
    if (world == nullptr) { return; }
    // The content outlives the tab it lived in: the next sync moves it into the tab of the world
    // that is active then.
    if (m_content->getSuperview() == world->host().get()) { m_content->removeFromSuperview(); }
    // The user closed the tab: the world leaves the list of the project on the next sync, not
    // from inside the dock's close.
    if (!m_closingTab) { m_closedByUser.push_back(world->worldId()); }
}

void WorldTabs::saveLayout(Panda::DockLayoutState &state) const {
    if (isWorldTab(state.selectedCenter)) { state.selectedCenter = LAYOUT_MARKER; }
}

void WorldTabs::restoreLayout(const Panda::DockLayoutState &state) {
    if (state.selectedCenter == LAYOUT_MARKER) { m_docks.activateTransientPanel(tabIdOf(activeWorldId())); }
}

} // namespace Editor
