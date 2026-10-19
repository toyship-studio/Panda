#include "UI/EditorTabs.hpp"

#include "Preview/ThumbnailsManager.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Project/System/SystemTools.hpp"
#include "UI/Documents/AssetEditorView.hpp"
#include "UI/Widgets/ObjectIcons.hpp"

#include <PandaEditorKit/EditorExtensions.hpp>
#include <PandaEditorKit/Widgets/Icons.hpp>

#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/GameLogic/World.hpp>

#include <algorithm>
#include <cstdlib>

namespace Editor {

namespace {

    // Tab keys: the id reflects the object — the "already open" deduplication works by it.
    std::string entityTabId(Panda::UUID id) {
        return "edit:e:" + std::to_string(static_cast<uint32_t>(id));
    }
    std::string assetTabId(Panda::AssetId id) {
        return "edit:a:" + std::to_string(static_cast<uint32_t>(id));
    }
    std::string documentTabId(const std::string &typeId) {
        return "doc:" + typeId;
    }
    constexpr const char *MULTI_TAB_ID = "edit:multi"; // multi-selection: lives only as a preview
    constexpr int HISTORY_CAP = 50; // depth of the selection history

    Panda::AssetHandlerEditor *assetHandler() {
        return static_cast<Panda::AssetHandlerEditor *>(Panda::GameContext::getAssetHandler());
    }

    // Substitution of the edit target with the tab's entities — only when there are any.
    class TargetGuard final {
    public:
        TargetGuard(EditorTabsHost *host, const std::vector<Panda::UUID> &ids)
            : m_host(ids.empty() ? nullptr : host) {
            if (m_host) { m_host->tabsBeginEntityTarget(ids); }
        }
        ~TargetGuard() {
            if (m_host) { m_host->tabsEndEntityTarget(); }
        }
        TargetGuard(const TargetGuard &) = delete;
        TargetGuard &operator=(const TargetGuard &) = delete;

    private:
        EditorTabsHost *m_host;
    };

} // namespace

// Per-tab Output: delegates entity edits to the controller, substituting the target with the
// entities of ITS OWN tab (a pinned tab edits its own object, not the current selection).
// Structural edits (the set of components/scripts) reread the tab form themselves — the
// controller's pushSelectionToPanels updates only the tab of the current selection.
class EditorTabs::Proxy final : public EntityEditorOutput {
public:
    Proxy(EditorTabs *tabs, EditorTabsHost *host, std::string tabId)
        : m_tabs(tabs)
        , m_host(host)
        , m_tabId(std::move(tabId)) {}

    void setTabId(std::string id) {
        m_tabId = std::move(id);
    }

    void entityDidEditName(const std::string &name) override {
        TargetGuard g(m_host, targets());
        out().entityDidEditName(name);
    }
    void entityDidSetStatic(bool isStatic) override {
        TargetGuard g(m_host, targets());
        out().entityDidSetStatic(isStatic);
    }
    void entityDidEditPosition(glm::vec3 position) override {
        TargetGuard g(m_host, targets());
        out().entityDidEditPosition(position);
    }
    void entityDidEditRotation(glm::vec3 rotation) override {
        TargetGuard g(m_host, targets());
        out().entityDidEditRotation(rotation);
    }
    void entityDidEditScale(glm::vec3 scale) override {
        TargetGuard g(m_host, targets());
        out().entityDidEditScale(scale);
    }
    void entityDidAddComponent(ComponentType type) override {
        {
            TargetGuard g(m_host, targets());
            out().entityDidAddComponent(type);
        }
        m_tabs->refreshTab(m_tabId); // structural — the tab rereads its sections
    }
    void entityDidRemoveComponent(ComponentType type) override {
        {
            TargetGuard g(m_host, targets());
            out().entityDidRemoveComponent(type);
        }
        m_tabs->refreshTab(m_tabId);
    }
    void entityDidEditComponent(const ComponentEdit &edit) override {
        TargetGuard g(m_host, targets());
        out().entityDidEditComponent(edit);
    }
    void entityDidAddExtensionComponent(const std::string &component) override {
        {
            TargetGuard g(m_host, targets());
            out().entityDidAddExtensionComponent(component);
        }
        m_tabs->refreshTab(m_tabId);
    }
    void entityDidRemoveExtensionComponent(const std::string &component) override {
        {
            TargetGuard g(m_host, targets());
            out().entityDidRemoveExtensionComponent(component);
        }
        m_tabs->refreshTab(m_tabId);
    }
    void entityDidAddScript(const std::string &className) override {
        {
            TargetGuard g(m_host, targets());
            out().entityDidAddScript(className);
        }
        m_tabs->refreshTab(m_tabId);
    }
    void entityDidRemoveScript(int scriptIndex) override {
        {
            TargetGuard g(m_host, targets());
            out().entityDidRemoveScript(scriptIndex);
        }
        m_tabs->refreshTab(m_tabId);
    }
    void entityDidEditScriptField(int scriptIndex, const std::string &fieldName, Panda::ScriptFieldValue value) override {
        TargetGuard g(m_host, targets());
        out().entityDidEditScriptField(scriptIndex, fieldName, std::move(value));
    }
    void entityDidClickBackground() override {
        out().entityDidClickBackground(); // globally: clear the selection
    }
    void entityDidSelectAsset(Panda::AssetId assetId) override {
        out().entityDidSelectAsset(assetId); // global selection → the asset tab
    }
    void entityDidRequestMaximize() override {
        m_tabs->requestMaximize(m_tabId); // zoom of the dock of EXACTLY this tab
    }

private:
    EntityEditorOutput &out() {
        return m_host->tabsHostOutput();
    }
    std::vector<Panda::UUID> targets() const {
        return m_tabs->entityTargets(m_tabId);
    }

    EditorTabs *m_tabs;
    EditorTabsHost *m_host;
    std::string m_tabId;
};

// An entity tab: a view of the world document — the form over the live targets; the edits dirty
// the world and go into its undo stack.
class EditorTabs::EntityDocument final : public Document {
public:
    EntityDocument(EditorTabs *tabs, EntityEditorOutput *output)
        : m_tabs(tabs)
        , m_view(std::make_shared<EntityEditorView>(output)) {
        m_view->setScriptClassProvider([host = tabs->m_host] { return host->tabsScriptClasses(); });
    }

    std::string title() override {
        const TabEntry *entry = m_tabs->findEntry(this);
        return entry ? m_tabs->titleFor(*entry) : "Entity";
    }
    std::string icon() override {
        const TabEntry *entry = m_tabs->findEntry(this);
        return entry ? m_tabs->iconFor(*entry) : Ic::EntityCube;
    }
    std::shared_ptr<PandaUI::View> view() override {
        return m_view;
    }
    Panda::CommandHistory *undoStack() override {
        Panda::World *world = Panda::GameContext::getCurrentWorld();
        return world ? &world->getCommandHistory() : nullptr;
    }
    void refresh() override {
        if (TabEntry *entry = m_tabs->findEntry(this)) { m_tabs->bindEntry(*entry); }
    }
    EntityEditorView &form() {
        return *m_view;
    }

private:
    EditorTabs *m_tabs;
    std::shared_ptr<EntityEditorView> m_view;
};

// An asset tab: the document of the asset — dirty state, Save and revert through the asset
// documents of the project, the undo stack per asset.
class EditorTabs::AssetDocument final : public Document {
public:
    explicit AssetDocument(EditorTabs *tabs)
        : m_tabs(tabs)
        , m_view(std::make_shared<AssetEditorView>([this] { m_tabs->documentEdited(this); })) {}

    std::string title() override {
        const TabEntry *entry = m_tabs->findEntry(this);
        return entry ? m_tabs->titleFor(*entry) : "Asset";
    }
    std::string icon() override {
        const TabEntry *entry = m_tabs->findEntry(this);
        return entry ? m_tabs->iconFor(*entry) : Ic::FileGeneric;
    }
    std::shared_ptr<PandaUI::View> view() override {
        return m_view;
    }
    bool isDirty() override {
        Panda::AssetHandlerEditor *handler = assetHandler();
        if (!handler) { return false; }
        for (Panda::AssetId id : assets()) {
            if (handler->documents().isAssetChanged(id)) { return true; }
        }
        return false;
    }
    bool save() override {
        Panda::AssetHandlerEditor *handler = assetHandler();
        if (!handler) { return false; }
        for (Panda::AssetId id : assets()) {
            if (handler->documents().isAssetChanged(id) && !handler->documents().saveAsset(id)) { return false; }
        }
        return true;
    }
    void revert() override {
        // Back to the file: otherwise the dirty state would remain in memory INVISIBLE (no tab) — a
        // violation of the invariant "the source of truth about what is unsaved is the open tabs".
        Panda::AssetHandlerEditor *handler = assetHandler();
        if (!handler) { return; }
        for (Panda::AssetId id : assets()) {
            if (handler->documents().isAssetChanged(id)) { handler->reload(id); }
        }
    }
    Panda::CommandHistory *undoStack() override {
        Panda::AssetHandlerEditor *handler = assetHandler();
        const std::vector<Panda::AssetId> ids = assets();
        return handler && ids.size() == 1 ? &handler->documents().undoStackFor(ids.front()) : nullptr;
    }
    void refresh() override {
        // Undo/redo and reloads change the live asset bypassing the form: the form rereads it,
        // the preview and the thumbnail are rendered again.
        if (auto *thumbnails = Panda::ThumbnailsManager::shared()) {
            for (Panda::AssetId id : assets()) { thumbnails->invalidate(id); }
        }
        m_view->rebuild();
    }
    AssetEditorView &form() {
        return *m_view;
    }

private:
    std::vector<Panda::AssetId> assets() const {
        const TabEntry *entry = m_tabs->findEntry(this);
        return entry ? entry->assetIds : std::vector<Panda::AssetId>{};
    }

    EditorTabs *m_tabs;
    std::shared_ptr<AssetEditorView> m_view;
};

/* ------------------------------ EditorTabs ------------------------------ */

EditorTabs::EditorTabs(DockManager *docks, EditorTabsHost *host)
    : m_docks(docks)
    , m_host(host) {
    m_docks->setOnTransientClosed([this](const std::string &id) {
        if (id == m_previewId) { m_previewId.clear(); }
        if (const TabEntry *entry = findEntry(id)) {
            // The terrain brush belongs to the asset tab — without it the tool is released.
            if (entry->assetIds.size() == 1) { m_host->tabsAssetTabClosed(entry->assetIds.front()); }
            m_host->tabsDocumentClosed(entry->document.get());
        }
        m_entries.erase(
            std::remove_if(
                m_entries.begin(), m_entries.end(), [&](const TabEntry &e) { return e.id == id; }
            ),
            m_entries.end()
        );
    });
    m_docks->setOnPanelPinned([this](const std::string &id) { pinTab(id); });
    // Close gate: a dirty document asks Save / Don't Save / Cancel.
    m_docks->setOnTransientCloseRequest([this](const std::string &id) { return shouldCloseTab(id); });
}

EditorTabs::~EditorTabs() = default;

void EditorTabs::pinTab(const std::string &tabId) {
    if (tabId != m_previewId) { return; } // already pinned
    m_previewId.clear(); // the tab got pinned — the next selection opens a new preview
    // A pinned multi slice gets a unique id: "edit:multi" is reserved for the preview,
    // otherwise the next multi-selection would activate this slice instead of a new one.
    if (tabId == MULTI_TAB_ID) {
        const std::string uniqueId = std::string(MULTI_TAB_ID) + ":" + std::to_string(m_pinnedMultiCounter++);
        if (TabEntry *entry = findEntry(tabId)) {
            m_docks->renameTransientPanel(tabId, uniqueId);
            entry->id = uniqueId;
            if (entry->proxy) { entry->proxy->setTabId(uniqueId); }
        }
    }
}

void EditorTabs::documentEdited(Document *document) {
    TabEntry *entry = findEntry(document);
    if (!entry) { return; }
    if (entry->id == m_previewId) {
        m_docks->setPanelPreview(entry->id, false); // the italic is removed
        pinTab(entry->id);
    }
}

bool EditorTabs::shouldCloseTab(const std::string &tabId) {
    TabEntry *entry = findEntry(tabId);
    if (!entry || !entry->document->isDirty()) { return true; } // entity tabs and clean documents close freely
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
        // The dirty state may have come bypassing the form (the terrain brush dirties the asset from the
        // viewport) — a preview with unsaved changes gets pinned here as well.
        if (dirty) { documentEdited(entry.document.get()); }
    }
}

std::vector<Document *> EditorTabs::dirtyDocuments() const {
    std::vector<Document *> dirty;
    for (const TabEntry &entry : m_entries) {
        if (entry.document->isDirty()) { dirty.push_back(entry.document.get()); }
    }
    return dirty;
}

bool EditorTabs::saveDirtyDocuments() {
    bool ok = true;
    for (Document *document : dirtyDocuments()) {
        if (!document->save()) { ok = false; }
    }
    return ok;
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
    for (const TabEntry &entry : m_entries) {
        if (entry.assetIds.size() == 1 && entry.assetIds.front() == assetId) { return entry.document.get(); }
    }
    return nullptr;
}

Document *EditorTabs::documentOfType(const std::string &typeId) const {
    const TabEntry *entry = findEntry(documentTabId(typeId));
    return entry ? entry->document.get() : nullptr;
}

void EditorTabs::refreshTabsForAsset(Panda::AssetId assetId) {
    std::vector<std::string> ids;
    for (const TabEntry &entry : m_entries) {
        if (entry.assetIds.size() == 1 && entry.assetIds.front() == assetId) { ids.push_back(entry.id); }
    }
    for (const std::string &id : ids) { refreshTab(id); }
}

std::vector<Panda::UUID> EditorTabs::entityTargets(const std::string &tabId) {
    TabEntry *entry = findEntry(tabId);
    return entry ? entry->entityIds : std::vector<Panda::UUID>{};
}

void EditorTabs::requestMaximize(const std::string &tabId) {
    if (TabEntry *entry = findEntry(tabId)) { m_host->tabsRequestMaximize(entry->document->view().get()); }
}

const char *EditorTabs::iconFor(const TabEntry &entry) const {
    if (!entry.entityIds.empty()) {
        const std::vector<Panda::Entity> alive = m_host->tabsResolveEntities(entry.entityIds);
        return alive.size() == 1 ? entityIconName(alive.front()) : Ic::EntityCube;
    }
    if (entry.assetIds.size() == 1) {
        if (Panda::AssetHandlerEditor *handler = assetHandler()) {
            auto it = handler->getRegistry().find(entry.assetIds.front());
            if (it != handler->getRegistry().end()) { return assetIconName(it->second.type); }
        }
    }
    return Ic::EntityCube;
}

std::string EditorTabs::titleFor(const TabEntry &entry) const {
    if (!entry.entityIds.empty()) {
        const std::vector<Panda::Entity> alive = m_host->tabsResolveEntities(entry.entityIds);
        if (alive.size() == 1) {
            const std::string name = alive.front().getName();
            return name.empty() ? "Entity" : name;
        }
        return std::to_string(alive.size()) + " entities";
    }
    if (entry.assetIds.size() == 1) {
        if (Panda::AssetHandlerEditor *handler = assetHandler()) {
            auto it = handler->getRegistry().find(entry.assetIds.front());
            if (it != handler->getRegistry().end()) { return handler->getAssetName(it->first); }
        }
        return "Asset";
    }
    return std::to_string(entry.assetIds.size()) + " assets";
}

void EditorTabs::bindEntry(TabEntry &entry) {
    if (auto *entity = dynamic_cast<EntityDocument *>(entry.document.get())) {
        const std::vector<Panda::Entity> alive = m_host->tabsResolveEntities(entry.entityIds);
        entry.aliveCount = alive.size();
        entity->form().setEntities(alive);
        return;
    }
    if (auto *asset = dynamic_cast<AssetDocument *>(entry.document.get())) {
        size_t alive = 0;
        Panda::AssetHandlerEditor *handler = assetHandler();
        for (Panda::AssetId id : entry.assetIds) {
            if (handler && handler->hasAsset(id)) { ++alive; }
        }
        entry.aliveCount = alive;
        asset->form().setAssets(entry.assetIds);
    }
}

EditorTabs::TabEntry EditorTabs::makeEntityEntry(std::string id, std::vector<Panda::UUID> entityIds) {
    TabEntry entry;
    entry.id = std::move(id);
    entry.entityIds = std::move(entityIds);
    entry.proxy = std::make_unique<Proxy>(this, m_host, entry.id);
    auto document = std::make_unique<EntityDocument>(this, entry.proxy.get());
    document->form().setRuntimeMode(m_runtimeMode);
    entry.document = std::move(document);
    return entry;
}

EditorTabs::TabEntry EditorTabs::makeAssetEntry(std::string id, std::vector<Panda::AssetId> assetIds) {
    TabEntry entry;
    entry.id = std::move(id);
    entry.assetIds = std::move(assetIds);
    entry.document = std::make_unique<AssetDocument>(this);
    return entry;
}

void EditorTabs::openEntry(TabEntry entry, Dock dock, bool preview) {
    DockPanel panel;
    panel.id = entry.id;
    // An object tab is titled by its targets (the entry is not in the list yet, so the document
    // cannot resolve them itself); a document of the registry names itself.
    panel.title = entry.typeId.empty() ? titleFor(entry) : entry.document->title();
    panel.icon = entry.typeId.empty() ? std::string(iconFor(entry)) : entry.document->icon();
    panel.content = entry.document->view();
    panel.dock = dock;
    panel.preview = preview;
    const bool isAssetTab = entry.assetIds.size() == 1;
    const Panda::AssetId tabAsset = isAssetTab ? entry.assetIds.front() : Panda::AssetId(0);
    m_entries.push_back(std::move(entry));
    if (isAssetTab) { m_host->tabsAssetTabOpened(tabAsset); }
    m_docks->openTransientPanel(std::move(panel), dock);
}

void EditorTabs::showSelection(
    const std::vector<Panda::Entity> &entities, const std::vector<Panda::AssetId> &assets
) {
    if (entities.empty() && assets.empty()) { return; } // clearing the selection does not touch the tabs

    std::vector<Panda::UUID> entityIds;
    for (const Panda::Entity &e : entities) { entityIds.push_back(e.getId()); }
    if (!m_navigating) { pushHistory({entityIds, assets}); } // navigation does not record itself again

    std::string id;
    if (entities.size() == 1 && assets.empty()) {
        id = entityTabId(entityIds.front());
    } else if (entities.empty() && assets.size() == 1) {
        id = assetTabId(assets.front());
    } else {
        id = MULTI_TAB_ID;
    }

    // The object is already open (pinned, or it is the preview itself) — activate without a duplicate.
    if (TabEntry *existing = findEntry(id)) {
        if (id == MULTI_TAB_ID) {
            // Multi-preview: the selection contents may have changed — reread the targets. A slice
            // mixes the kinds only in theory: entities and assets are selected mutually exclusively.
            const bool sameKind = existing->entityIds.empty() == entityIds.empty();
            if (sameKind) {
                existing->entityIds = std::move(entityIds);
                existing->assetIds = assets;
                bindEntry(*existing);
                m_docks->updateTransientPanel(id, titleFor(*existing), iconFor(*existing), existing->document->view());
            } else {
                m_docks->closeTransientPanel(id); // the kind changed: a new document of the other kind
                showSelection(entities, assets);
                return;
            }
        }
        m_docks->activateTransientPanel(id);
        return;
    }

    if (!m_previewId.empty() && m_docks->panelExists(m_previewId)) {
        // Replace the preview in place when the document kind matches: the tab and its form are
        // reused, the targets and the id change. Another kind: the preview tab closes and a new
        // one opens in its place.
        TabEntry *entry = findEntry(m_previewId);
        const bool entityKind = !entityIds.empty();
        if (entry && (entry->entityIds.empty() != entityKind)) {
            m_docks->renameTransientPanel(m_previewId, id);
            entry->id = id;
            if (entry->proxy) { entry->proxy->setTabId(id); }
            entry->entityIds = std::move(entityIds);
            entry->assetIds = assets;
            bindEntry(*entry);
            m_docks->updateTransientPanel(id, titleFor(*entry), iconFor(*entry), entry->document->view());
            m_docks->activateTransientPanel(id);
            m_previewId = id;
            return;
        }
        if (entry) { m_docks->closeTransientPanel(m_previewId); }
        m_previewId.clear();
    }

    // A new preview tab in the right dock.
    TabEntry entry = entityIds.empty() ? makeAssetEntry(id, assets) : makeEntityEntry(id, std::move(entityIds));
    bindEntry(entry);
    const std::string newId = entry.id;
    openEntry(std::move(entry), Dock::Right, /*preview*/ true);
    m_previewId = newId;
}

void EditorTabs::openDocumentType(const std::string &typeId, Dock dock) {
    const std::string id = documentTabId(typeId);
    if (m_docks->activateTransientPanel(id)) { return; }
    const DocumentType *type = editorExtensions().documentTypes.find(typeId);
    if (type == nullptr || !type->create) { return; }
    TabEntry entry;
    entry.id = id;
    entry.typeId = typeId;
    entry.document = type->create();
    if (!entry.document) { return; }
    openEntry(std::move(entry), dock, /*preview*/ false);
}

void EditorTabs::setRuntimeMode(bool runtime) {
    m_runtimeMode = runtime;
    for (TabEntry &entry : m_entries) {
        if (auto *entity = dynamic_cast<EntityDocument *>(entry.document.get())) { entity->form().setRuntimeMode(runtime); }
    }
}

void EditorTabs::refreshTransforms() {
    for (TabEntry &entry : m_entries) {
        if (auto *entity = dynamic_cast<EntityDocument *>(entry.document.get())) { entity->form().refreshTransform(); }
    }
}

void EditorTabs::refreshTab(const std::string &tabId) {
    if (TabEntry *entry = findEntry(tabId)) {
        bindEntry(*entry);
        if (!entry->typeId.empty()) {
            entry->document->refresh();
            return;
        }
        m_docks->updateTransientPanel(entry->id, titleFor(*entry), iconFor(*entry), entry->document->view());
    }
}

void EditorTabs::syncLifetimes() {
    // Death of targets (undo/deletion/reload): a tab without live targets is closed, a partially
    // live one (multi) is narrowed down to the live ones. Cheap detection by the live count, a rebuild —
    // only when it actually happened. Documents of the registry have no targets.
    std::vector<std::string> toClose;
    Panda::AssetHandlerEditor *handler = assetHandler();
    for (TabEntry &entry : m_entries) {
        if (!entry.typeId.empty()) { continue; }
        size_t alive = m_host->tabsResolveEntities(entry.entityIds).size();
        for (Panda::AssetId assetId : entry.assetIds) {
            if (handler && handler->hasAsset(assetId)) { ++alive; }
        }
        if (alive == entry.aliveCount) { continue; }
        if (alive == 0) {
            toClose.push_back(entry.id);
        } else {
            refreshTab(entry.id);
        }
    }
    for (const std::string &id : toClose) {
        m_docks->closeTransientPanel(id); // onTransientClosed removes the TabEntry
    }
}

void EditorTabs::closeAllEntityTabs() {
    std::vector<std::string> toClose;
    for (TabEntry &entry : m_entries) {
        if (!entry.entityIds.empty()) { toClose.push_back(entry.id); }
    }
    for (const std::string &id : toClose) { m_docks->closeTransientPanel(id); }
}

/* ------------------------------ Selection history ------------------------------ */

void EditorTabs::pushHistory(HistoryEntry entry) {
    if (m_historyCursor >= 0 && m_historyCursor < static_cast<int>(m_history.size())) {
        const HistoryEntry &current = m_history[m_historyCursor];
        if (current.entities == entry.entities && current.assets == entry.assets) {
            return; // a repeated click
        }
    }
    m_history.resize(m_historyCursor + 1); // a new selection cuts off the forward branch (as in a browser)
    m_history.push_back(std::move(entry));
    if (static_cast<int>(m_history.size()) > HISTORY_CAP) { m_history.erase(m_history.begin()); }
    m_historyCursor = static_cast<int>(m_history.size()) - 1;
    notifyHistory();
}

void EditorTabs::navigateTo(int cursor) {
    if (cursor < 0 || cursor >= static_cast<int>(m_history.size())) { return; }
    m_historyCursor = cursor;
    const HistoryEntry &entry = m_history[m_historyCursor];
    m_navigating = true;
    // The regular selection path: Hierarchy/highlight get updated, the tab opens/is restored
    // through showSelection. The controller filters out dead targets itself.
    m_host->tabsApplySelection(entry.entities, entry.assets);
    m_navigating = false;
    notifyHistory();
}

void EditorTabs::goBack() {
    navigateTo(m_historyCursor - 1);
}

void EditorTabs::goForward() {
    navigateTo(m_historyCursor + 1);
}

bool EditorTabs::canGoBack() const {
    return m_historyCursor > 0;
}

bool EditorTabs::canGoForward() const {
    return m_historyCursor + 1 < static_cast<int>(m_history.size());
}

void EditorTabs::clearHistory() {
    m_history.clear();
    m_historyCursor = -1;
    notifyHistory(); // the Toolbar buttons get dimmed
}

void EditorTabs::notifyHistory() {
    m_host->tabsHistoryChanged(canGoBack(), canGoForward());
}

/* ------------------------------ Persist of pinned tabs ------------------------------ */

void EditorTabs::restorePinned(const std::vector<Panda::PinnedTabPlacement> &pinned) {
    Panda::AssetHandlerEditor *handler = assetHandler();
    for (const Panda::PinnedTabPlacement &placement : pinned) {
        if (m_docks->panelExists(placement.id)) { continue; }
        const Dock dock = static_cast<Dock>(std::clamp(placement.dock, 0, 3));
        const std::string &id = placement.id;
        // The id encodes the object: edit:e:<uuid> / edit:a:<id> / doc:<type>. The rest (multi,
        // outdated) is skipped.
        auto parseTail = [&](size_t prefixLen) -> uint32_t {
            return static_cast<uint32_t>(std::strtoul(id.c_str() + prefixLen, nullptr, 10));
        };
        if (id.rfind("doc:", 0) == 0) {
            openDocumentType(id.substr(4), dock); // an unknown type is simply not restored
            continue;
        }
        std::vector<Panda::UUID> entityIds;
        std::vector<Panda::AssetId> assetIds;
        if (id.rfind("edit:e:", 0) == 0) {
            entityIds.push_back(Panda::UUID(parseTail(7)));
        } else if (id.rfind("edit:a:", 0) == 0) {
            assetIds.push_back(Panda::AssetId(parseTail(7)));
        } else {
            continue;
        }
        // Dead targets are not restored (another world/a deleted asset).
        size_t alive = m_host->tabsResolveEntities(entityIds).size();
        for (Panda::AssetId assetId : assetIds) {
            if (handler && handler->hasAsset(assetId)) { ++alive; }
        }
        if (alive == 0) { continue; }
        TabEntry entry = entityIds.empty() ? makeAssetEntry(id, std::move(assetIds)) : makeEntityEntry(id, std::move(entityIds));
        bindEntry(entry);
        openEntry(std::move(entry), dock, /*preview*/ false); // restored ones are pinned by definition
    }
}

} // namespace Editor
