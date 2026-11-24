#include "UI/EditorTabs.hpp"

#include "Preview/AssetPreviews.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Project/System/SystemTools.hpp"
#include "UI/Documents/AssetEditorView.hpp"
#include <PandaEditorKit/Widgets/ObjectIcons.hpp>

#include <PandaEditorKit/EditorExtensions.hpp>
#include <PandaEditorKit/Widgets/Icons.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace Editor {

namespace {

    // Tab keys: the id names the document — the "already open" deduplication works by it.
    constexpr const char *ASSET_TAB_PREFIX = "asset:";
    constexpr const char *DOCUMENT_TAB_PREFIX = "doc:";

    std::string assetTabId(Panda::AssetId id) {
        return ASSET_TAB_PREFIX + std::to_string(static_cast<uint32_t>(id));
    }
    std::string documentTabId(const std::string &typeId) {
        return DOCUMENT_TAB_PREFIX + typeId;
    }

} // namespace

// An asset tab: the document of the asset — dirty state, Save and revert through the asset
// documents of the project, the undo stack of the asset.
class EditorTabs::AssetDocument final : public Document {
public:
    AssetDocument(EditorTabs *tabs, Panda::AssetId asset)
        : m_tabs(tabs)
        , m_asset(asset)
        , m_view(std::make_shared<AssetEditorView>()) {
        m_view->setAccessibilityId("assetEditor"); // what a UI test finds the form of an asset by
        m_view->setAsset(asset);
    }

    std::string title() override {
        return m_tabs->assetTitle(m_asset);
    }
    std::string icon() override {
        return m_tabs->assetIcon(m_asset);
    }
    std::shared_ptr<PandaUI::View> view() override {
        return m_view;
    }
    bool isDirty() override {
        Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
        return handler && handler->documents().isAssetChanged(m_asset);
    }
    bool save() override {
        return !isDirty() || m_tabs->m_host->tabsSaveAsset(m_asset);
    }
    std::filesystem::path filePath() override {
        Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
        if (!handler) { return {}; }
        const Panda::path_t relative = handler->assetPath(m_asset);
        return relative.empty() ? std::filesystem::path() : handler->getProjectPath() / relative;
    }
    void revert() override {
        // Back to the file: otherwise the dirty state would remain in memory INVISIBLE (no tab) — a
        // violation of the invariant "the source of truth about what is unsaved is the open tabs".
        Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
        if (handler && handler->documents().isAssetChanged(m_asset)) { handler->reload(m_asset); }
    }
    Panda::CommandHistory *undoStack() override {
        Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
        return handler ? &handler->documents().undoStackFor(m_asset) : nullptr;
    }
    void refresh() override {
        // Undo/redo and reloads change the live asset bypassing the form: the form rereads it,
        // the preview and the thumbnail are rendered again.
        if (auto *thumbnails = Panda::AssetPreviews::shared()) { thumbnails->invalidate(m_asset); }
        m_view->rebuild();
    }

private:
    EditorTabs *m_tabs;
    Panda::AssetId m_asset;
    std::shared_ptr<AssetEditorView> m_view;
};

/* ------------------------------ EditorTabs ------------------------------ */

EditorTabs::EditorTabs(DockManager *docks, EditorTabsHost *host)
    : m_docks(docks)
    , m_host(host) {
    m_docks->setOnTransientClosed([this](const std::string &id) {
        if (const TabEntry *entry = findEntry(id)) {
            // The terrain brush belongs to the asset tab — without it the tool is released.
            if (entry->assetId) { m_host->tabsAssetTabClosed(entry->assetId); }
            m_host->tabsDocumentClosed(entry->document.get());
        }
        std::erase_if(m_entries, [&](const TabEntry &e) { return e.id == id; });
    });
    // Close gate: a dirty document asks Save / Don't Save / Cancel.
    m_docks->setOnTransientCloseRequest([this](const std::string &id) { return shouldCloseTab(id); });
}

EditorTabs::~EditorTabs() = default;

bool EditorTabs::shouldCloseTab(const std::string &tabId) {
    TabEntry *entry = findEntry(tabId);
    if (!entry || !entry->document->isDirty()) { return true; } // clean documents close freely
    // A dirty tab: the question is deferred to the NEXT frame with the tab activated —
    // a modal dialog freezes the render loop, and an activation made in the frame of the click would
    // not get drawn in time (the user would not see what the question is about). The close is cancelled
    // for now; the queue closes the tab itself after the answer.
    if (std::find(m_pendingCloseTabs.begin(), m_pendingCloseTabs.end(), tabId) == m_pendingCloseTabs.end()) {
        m_pendingCloseTabs.push_back(tabId);
    }
    m_docks->activateTransientPanel(tabId);
    return false;
}

void EditorTabs::processPendingCloseRequests() {
    if (m_pendingCloseTabs.empty()) { return; }
    // One question per frame: the tab activation has already been drawn by the previous frame.
    const std::string tabId = m_pendingCloseTabs.front();
    m_pendingCloseTabs.erase(m_pendingCloseTabs.begin());
    TabEntry *entry = findEntry(tabId);
    if (!entry) { return; } // the tab died (lifecycle) — the question is dropped
    if (!entry->document->isDirty()) { // got saved between the click and the question frame
        m_docks->closeTransientPanel(tabId);
        return;
    }
    const std::string message = "Save changes to \"" + entry->document->title() + "\"?";
    const auto choice = Panda::SystemTools::showSaveChangesDialog(message.c_str(), "Your changes will be lost otherwise.");
    switch (choice) {
        case Panda::SystemTools::SaveChangesChoice::Cancel:
            m_pendingCloseTabs.clear(); // a cancel also drops the rest of the batch (Close Others)
            return;
        case Panda::SystemTools::SaveChangesChoice::Save:
            if (!entry->document->save()) { // write error — do not lose the changes
                m_pendingCloseTabs.clear();
                return;
            }
            break;
        case Panda::SystemTools::SaveChangesChoice::DontSave:
            entry->document->revert();
            break;
    }
    m_docks->closeTransientPanel(tabId);
}

void EditorTabs::syncDirtyStates() {
    for (TabEntry &entry : m_entries) {
        const bool dirty = entry.document->isDirty();
        if (dirty == entry.lastDirty) { continue; }
        entry.lastDirty = dirty;
        m_docks->setPanelDirty(entry.id, dirty);
    }
}

EditorTabs::TabEntry *EditorTabs::findEntry(const std::string &id) {
    for (TabEntry &entry : m_entries) {
        if (entry.id == id) { return &entry; }
    }
    return nullptr;
}

const EditorTabs::TabEntry *EditorTabs::findEntry(const std::string &id) const {
    for (const TabEntry &entry : m_entries) {
        if (entry.id == id) { return &entry; }
    }
    return nullptr;
}

EditorTabs::TabEntry *EditorTabs::findEntry(const Document *document) {
    for (TabEntry &entry : m_entries) {
        if (entry.document.get() == document) { return &entry; }
    }
    return nullptr;
}

Document *EditorTabs::documentForTab(const std::string &tabId) const {
    const TabEntry *entry = findEntry(tabId);
    return entry ? entry->document.get() : nullptr;
}

Document *EditorTabs::documentForAsset(Panda::AssetId assetId) const {
    const TabEntry *entry = findEntry(assetTabId(assetId));
    return entry ? entry->document.get() : nullptr;
}

Document *EditorTabs::documentOfType(const std::string &typeId) const {
    const TabEntry *entry = findEntry(documentTabId(typeId));
    return entry ? entry->document.get() : nullptr;
}

void EditorTabs::refreshTabsForAsset(Panda::AssetId assetId) {
    if (TabEntry *entry = findEntry(assetTabId(assetId))) {
        entry->document->refresh();
        m_docks->setPanelTitle(entry->id, entry->document->title()); // a rename shows on the tab
    }
}

std::string EditorTabs::assetTitle(Panda::AssetId asset) const {
    if (Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current()) {
        auto it = handler->getRegistry().find(asset);
        if (it != handler->getRegistry().end()) { return handler->getAssetName(it->first); }
    }
    return "Asset";
}

const char *EditorTabs::assetIcon(Panda::AssetId asset) const {
    if (Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current()) {
        auto it = handler->getRegistry().find(asset);
        if (it != handler->getRegistry().end()) { return assetIconName(it->second.type); }
    }
    return Ic::FileGeneric;
}

void EditorTabs::openEntry(TabEntry entry, Dock dock, bool pinnedToDock) {
    DockPanel panel;
    panel.movable = !pinnedToDock;
    panel.restored = !pinnedToDock;
    panel.id = entry.id;
    panel.title = entry.document->title();
    panel.icon = entry.document->icon();
    panel.content = entry.document->view();
    panel.dock = dock;
    const Panda::AssetId tabAsset = entry.assetId;
    m_entries.push_back(std::move(entry));
    if (tabAsset) { m_host->tabsAssetTabOpened(tabAsset); }
    m_docks->openTransientPanel(std::move(panel), dock);
}

void EditorTabs::openAssetTab(Panda::AssetId asset, Dock dock) {
    TabEntry entry;
    entry.id = assetTabId(asset);
    entry.assetId = asset;
    entry.document = std::make_unique<AssetDocument>(this, asset);
    openEntry(std::move(entry), dock);
}

bool EditorTabs::openAsset(Panda::AssetId asset) {
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    if (!asset || handler == nullptr || !handler->hasAsset(asset)) { return false; }
    // Open already — in the dock the user moved it to — it comes forward.
    if (m_docks->activateTransientPanel(assetTabId(asset))) { return true; }
    openAssetTab(asset, Dock::Center);
    return true;
}

void EditorTabs::openDocumentType(const std::string &typeId, Dock dock) {
    const std::string id = documentTabId(typeId);
    if (m_docks->activateTransientPanel(id)) { return; }
    const DocumentType *type = editorExtensions().documentTypes.find(typeId);
    if (type == nullptr || !type->create) { return; }
    TabEntry entry;
    entry.id = id;
    entry.document = type->create();
    if (!entry.document) { return; }
    openEntry(std::move(entry), dock);
}

void EditorTabs::openDocument(std::string tabId, std::unique_ptr<Document> document, Dock dock) {
    if (!document || m_docks->activateTransientPanel(tabId)) { return; }
    TabEntry entry;
    entry.id = std::move(tabId);
    entry.document = std::move(document);
    openEntry(std::move(entry), dock, /*pinnedToDock*/ true);
}

void EditorTabs::closeTab(const std::string &tabId) {
    m_docks->closeTransientPanel(tabId); // onTransientClosed removes the TabEntry
}

void EditorTabs::syncLifetimes() {
    // The asset of a tab is gone (its file was deleted, a reload of the registry): the tab closes.
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    std::vector<std::string> toClose;
    for (const TabEntry &entry : m_entries) {
        if (entry.assetId && (handler == nullptr || !handler->hasAsset(entry.assetId))) { toClose.push_back(entry.id); }
    }
    for (const std::string &id : toClose) { m_docks->closeTransientPanel(id); }
}

void EditorTabs::closeAllAssetTabs() {
    std::vector<std::string> toClose;
    for (const TabEntry &entry : m_entries) {
        if (entry.assetId) { toClose.push_back(entry.id); }
    }
    for (const std::string &id : toClose) { m_docks->closeTransientPanel(id); }
}

/* ------------------------------ The tabs of the layout ------------------------------ */

void EditorTabs::restoreTabs(const std::vector<Panda::OpenTabPlacement> &tabs) {
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    for (const Panda::OpenTabPlacement &placement : tabs) {
        if (m_docks->panelExists(placement.id)) { continue; }
        const Dock dock = static_cast<Dock>(std::clamp(placement.dock, 0, 3));
        const std::string &id = placement.id;
        if (id.rfind(DOCUMENT_TAB_PREFIX, 0) == 0) {
            openDocumentType(id.substr(std::strlen(DOCUMENT_TAB_PREFIX)), dock); // an unknown type is not restored
            continue;
        }
        if (id.rfind(ASSET_TAB_PREFIX, 0) == 0) {
            const Panda::AssetId asset(
                static_cast<uint32_t>(std::strtoul(id.c_str() + std::strlen(ASSET_TAB_PREFIX), nullptr, 10))
            );
            if (handler && handler->hasAsset(asset)) { openAssetTab(asset, dock); } // a deleted asset is not restored
        }
    }
}

} // namespace Editor
