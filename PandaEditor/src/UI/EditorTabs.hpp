#pragma once

#include "Project/Settings/EditorSettings.hpp" // Panda::PinnedTabPlacement (tab persist)
#include "UI/DockManager.hpp"
#include "UI/Documents/EntityEditorView.hpp"

#include <PandaEditorKit/Document.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Editor {

// The coordinator's bridge for editor tabs: applying entity edits (the controller's Output methods
// hit the "current selection" — for the duration of an edit from a tab the selection is substituted
// with its targets), resolving live objects and services.
class EditorTabsHost {
public:
    virtual ~EditorTabsHost() = default;
    virtual EntityEditorOutput &tabsHostOutput() = 0;
    // Edit target = the tab's entities (see the controller's selectedEntities). The empty end clears it.
    virtual void tabsBeginEntityTarget(const std::vector<Panda::UUID> &ids) = 0;
    virtual void tabsEndEntityTarget() = 0;
    virtual std::vector<Panda::Entity> tabsResolveEntities(const std::vector<Panda::UUID> &ids) = 0;
    virtual std::vector<std::string> tabsScriptClasses() = 0;
    virtual void tabsRequestMaximize(PandaUI::View *view) = 0;
    // The asset tab is closed: the viewport session with this document ends (the tool lets go).
    virtual void tabsAssetTabClosed(Panda::AssetId id) = 0;
    virtual void tabsAssetTabOpened(Panda::AssetId id) = 0; // counterpart: tracks openness in AssetEditorMeta
    // A document is about to go (its tab closed): the coordinator drops the pointers it keeps.
    virtual void tabsDocumentClosed(Document *document) = 0;
    // History navigation: apply the selection through the regular path (Hierarchy/highlight follow along).
    virtual void tabsApplySelection(
        const std::vector<Panda::UUID> &entities, const std::vector<Panda::AssetId> &assets
    ) = 0;
    virtual void tabsHistoryChanged(bool canGoBack, bool canGoForward) = 0; // Toolbar buttons
};

// The coordinator of the editor tabs (the document model, context/plugins.md). Every tab holds a
// Document: an entity tab is a view of the world document (its edits dirty the world), an asset
// tab is the document of the asset (its own undo stack, dirty state and Save), and the document
// types of the registry (the settings of the project and of the editor) open from the dock's "+"
// and the menu bar. Selecting an object opens a preview tab in the right dock: the next selection
// REPLACES its content, a double click on the tab pins it (the next selection goes into a new
// preview). An object already open in any tab is activated without a duplicate.
class EditorTabs final {
public:
    EditorTabs(DockManager *docks, EditorTabsHost *host);
    ~EditorTabs();

    // Current editor selection (from pushSelectionToPanels). An empty selection does not touch the tabs.
    void showSelection(const std::vector<Panda::Entity> &entities, const std::vector<Panda::AssetId> &assets);
    // A document of the registry (EditorExtensions::documentTypes) in the dock; an open one is activated.
    void openDocumentType(const std::string &typeId, Dock dock);

    void setRuntimeMode(bool runtime);   // the PLAY banner on all entity tabs
    void refreshTransforms();            // gizmo drag: a light refresh of the Transform fields
    void syncLifetimes();                // per frame: targets died → the tab is closed/narrowed
    void closeAllEntityTabs();           // world change: entity tabs point into the old world
    // The document of a tab (nullptr — not a tab of EditorTabs: the viewport and the panels).
    Document *documentForTab(const std::string &tabId) const;
    Document *documentForAsset(Panda::AssetId assetId) const; // the tab of exactly this asset
    Document *documentOfType(const std::string &typeId) const;
    // Rereads the forms of this asset's tabs (undo/redo edit the document bypassing the forms).
    void refreshTabsForAsset(Panda::AssetId assetId);
    // Per frame: the "unsaved" dot on a tab = ITS document isDirty (per tab, not one bool shared
    // by all). Cheap: a comparison with the cache of the previous value.
    void syncDirtyStates();
    // Per frame: deferred close questions of dirty tabs (the dialog comes a frame later than the tab
    // activation — a modal NSAlert freezes the render, the user must see what the question is about).
    void processPendingCloseRequests();
    void refreshTab(const std::string &tabId); // rereads the tab's targets (a structural edit)
    // The dirty documents of the open tabs (Save All and the close questions of the project).
    std::vector<Document *> dirtyDocuments() const;
    bool saveDirtyDocuments(); // false — a write failed

    // Selection history: back/forward select the object anew through the regular path — a closed tab
    // is RESTORED by that (the history stores objects, not tabs). Dead entries are skipped.
    void goBack();
    void goForward();
    void clearHistory(); // project change/close, another world: the entries point into a dead context
    bool canGoBack() const;
    bool canGoForward() const;

    // Persist: pinned tabs from the saved layout (after the world is loaded).
    void restorePinned(const std::vector<Panda::PinnedTabPlacement> &pinned);

private:
    class Proxy;
    class EntityDocument;
    class AssetDocument;
    friend class Proxy; // the proxy reaches for the targets/services of its tab
    friend class EntityDocument;
    friend class AssetDocument;

    std::vector<Panda::UUID> entityTargets(const std::string &tabId);
    void requestMaximize(const std::string &tabId);
    // An edit of an asset document from its form: the preview tab gets PINNED — otherwise the
    // next selection would hide a tab with unsaved changes.
    void documentEdited(Document *document);
    void pinTab(const std::string &tabId); // the common pinning path (double click / edit)
    // Close gate: clean tabs close immediately; dirty ones — activation + a deferred
    // question (processPendingCloseRequests: Save / Don't Save (revert to the file) / Cancel).
    bool shouldCloseTab(const std::string &tabId);

    struct TabEntry {
        std::string id;                       // tab id in DockManager ("edit:...", "doc:...")
        std::vector<Panda::UUID> entityIds;   // targets of an entity tab
        std::vector<Panda::AssetId> assetIds; // targets of an asset tab
        std::string typeId;                   // a document of the registry (empty — an object tab)
        size_t aliveCount = 0;                // live targets at the previous check (death detection)
        bool lastDirty = false;               // previous state of the "unsaved" dot
        // Member order: the entity form holds a raw pointer to proxy — document dies first.
        std::unique_ptr<Proxy> proxy;
        std::unique_ptr<Document> document;
    };

    TabEntry *findEntry(const std::string &id);
    const TabEntry *findEntry(const std::string &id) const;
    TabEntry *findEntry(const Document *document);
    TabEntry makeEntityEntry(std::string id, std::vector<Panda::UUID> entityIds);
    TabEntry makeAssetEntry(std::string id, std::vector<Panda::AssetId> assetIds);
    void bindEntry(TabEntry &entry); // the forms reread the live targets
    const char *iconFor(const TabEntry &entry) const;
    std::string titleFor(const TabEntry &entry) const;
    void openEntry(TabEntry entry, Dock dock, bool preview);

    // Selection history (objects, not tabs): back/forward move the cursor and select anew.
    struct HistoryEntry {
        std::vector<Panda::UUID> entities;
        std::vector<Panda::AssetId> assets;
    };
    void pushHistory(HistoryEntry entry);
    void navigateTo(int cursor);
    void notifyHistory();

    DockManager *m_docks;
    EditorTabsHost *m_host;
    std::vector<TabEntry> m_entries;
    std::string m_previewId; // id of the current preview tab (empty — none)
    int m_pinnedMultiCounter = 0; // unique ids of pinned multi slices
    bool m_runtimeMode = false;
    std::vector<HistoryEntry> m_history;
    int m_historyCursor = -1;   // index of the current entry (-1 — no history)
    bool m_navigating = false;  // a selection from goBack/goForward is not recorded into the history again
    std::vector<std::string> m_pendingCloseTabs; // queue of deferred close questions
};

} // namespace Editor
