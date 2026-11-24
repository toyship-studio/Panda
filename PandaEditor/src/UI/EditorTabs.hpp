#pragma once

#include "Project/Settings/EditorSettings.hpp" // Panda::OpenTabPlacement (the tabs of the layout)
#include "UI/DockManager.hpp"

#include <PandaEditorKit/Document.hpp>

#include <Panda/Assets/Base/Asset.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Editor {

// What the tabs of documents ask of the workspace: the asset a tab holds open, a document that
// goes, the saving of an asset (the conflict with the disk is asked there).
class EditorTabsHost {
public:
    virtual ~EditorTabsHost() = default;
    // The asset of a tab is held loaded while the tab is open (AssetHandlerEditor), and a viewport
    // session that edits it ends with its tab (the tool lets go).
    virtual void tabsAssetTabOpened(Panda::AssetId id) = 0;
    virtual void tabsAssetTabClosed(Panda::AssetId id) = 0;
    // A document is about to go (its tab closed): the coordinator drops the pointers it keeps.
    virtual void tabsDocumentClosed(Document *document) = 0;
    virtual bool tabsSaveAsset(Panda::AssetId id) = 0;
};

// The tabs of documents (the document model, context/architecture.md): a tab is a view of one
// document. An asset opens in a tab of its own — a double click in the Content Browser, Open of
// its menu, a double click on a well that names it — with the dirty dot, Save, Revert and the undo
// stack of that asset; the documents of the registry (the Plugins tab, the documents of modules)
// open from the "+" of a dock and from the menu bar; the coordinator puts its own documents (the
// open worlds) into tabs that stay in their dock. A document that is open already is brought
// forward, never opened twice.
class EditorTabs final {
public:
    EditorTabs(DockManager *docks, EditorTabsHost *host);
    ~EditorTabs();

    // The asset in its tab, in the center unless its tab is somewhere already. false — no such asset.
    bool openAsset(Panda::AssetId asset);
    // A document of the registry (EditorExtensions::documentTypes) in the dock; an open one is activated.
    void openDocumentType(const std::string &typeId, Dock dock);
    // A document the coordinator makes itself (an open world), in a tab that stays in its dock and
    // is not saved with the layout: the coordinator opens it again from its own list.
    void openDocument(std::string tabId, std::unique_ptr<Document> document, Dock dock);
    void closeTab(const std::string &tabId); // without a question: the document is gone already
    void closeAllAssetTabs();                // project change: asset tabs name the assets of the old project

    // The document of a tab (nullptr — not a tab of a document: the panels).
    Document *documentForTab(const std::string &tabId) const;
    Document *documentForAsset(Panda::AssetId assetId) const; // the tab of exactly this asset
    Document *documentOfType(const std::string &typeId) const;
    // Rereads the form of this asset's tab (undo/redo edit the document bypassing the form).
    void refreshTabsForAsset(Panda::AssetId assetId);
    // Per frame: the "unsaved" dot on a tab = ITS document isDirty. Cheap: a comparison with the
    // cache of the previous value.
    void syncDirtyStates();
    // Per frame: deferred close questions of dirty tabs (the dialog comes a frame later than the tab
    // activation — a modal NSAlert freezes the render, the user must see what the question is about).
    void processPendingCloseRequests();
    void syncLifetimes(); // per frame: the asset of a tab is gone → the tab closes

    // The tabs of the saved layout (after the project is loaded): asset tabs and the documents of
    // the registry; an asset that is gone is not opened.
    void restoreTabs(const std::vector<Panda::OpenTabPlacement> &tabs);

private:
    class AssetDocument;
    friend class AssetDocument;

    // Close gate: clean tabs close immediately; dirty ones — activation + a deferred
    // question (processPendingCloseRequests: Save / Don't Save (revert to the file) / Cancel).
    bool shouldCloseTab(const std::string &tabId);

    struct TabEntry {
        std::string id;              // tab id in DockManager ("asset:<id>", "doc:<type>", the coordinator's own)
        Panda::AssetId assetId = 0;  // the asset of an asset tab
        bool lastDirty = false;      // previous state of the "unsaved" dot
        std::unique_ptr<Document> document;
    };

    TabEntry *findEntry(const std::string &id);
    const TabEntry *findEntry(const std::string &id) const;
    TabEntry *findEntry(const Document *document);
    std::string assetTitle(Panda::AssetId asset) const;
    const char *assetIcon(Panda::AssetId asset) const;
    void openAssetTab(Panda::AssetId asset, Dock dock);
    // pinnedToDock — the tab stays in the dock and is not saved with the layout (openDocument).
    void openEntry(TabEntry entry, Dock dock, bool pinnedToDock = false);

    DockManager *m_docks;
    EditorTabsHost *m_host;
    std::vector<TabEntry> m_entries;
    std::vector<std::string> m_pendingCloseTabs; // queue of deferred close questions
};

} // namespace Editor
