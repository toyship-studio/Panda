#pragma once

#include "UI/DockManager.hpp"
#include "UI/EditorTabs.hpp"

#include <PandaEditorKit/Document.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Panda {
class EditorLayer;
class WorldFile;
} // namespace Panda

namespace Editor {

class WorldDocument;

// The open worlds of the project as tabs of the center dock (FileDocuments::worlds): a tab per
// world, titled with the name of its file, with the dirty dot, Save and Undo of that world. The
// tabs follow the list, the active world follows the selected tab (not while the game plays), and
// the one Scene viewport of the editor lives in the tab of the active world.
class WorldTabs final {
public:
    WorldTabs(Panda::EditorLayer &app, DockManager &docks, EditorTabs &tabs, std::shared_ptr<PandaUI::View> viewport);

    // Once a frame, and whenever the application changes the set of worlds or the active one.
    void sync();
    // A world of the project in a tab. preview — the unpinned tab (the title in italics): there is
    // one, the next world shown this way takes its place; a double click on the file or on the
    // tab, or an edit of the world, keeps the tab. A world that is open already only comes forward.
    void open(Panda::AssetId world, bool preview);
    void showActive(); // the tab of the active world comes forward
    // The document of the active world — its file, Save, the undo stack; nullptr — no world is open.
    Document *activeDocument();
    // A document goes with its tab. The tab of a world closed by the user takes the world out of
    // the list of the project on the next sync.
    void documentClosed(Document *document);

    // The layout of the session does not name a world tab — the ids of the open worlds live for
    // one run — it remembers that a world was in front of the center.
    void saveLayout(Panda::DockLayoutState &state) const;
    void restoreLayout(const Panda::DockLayoutState &state);

private:
    friend class WorldDocument;
    Panda::WorldFile *worldFile(uint32_t worldId) const; // the open world of a tab; nullptr — it is closed
    uint32_t activeWorldId() const;                      // 0 — no world is open
    WorldDocument *document(uint32_t worldId);

    Panda::EditorLayer &m_app;
    DockManager &m_docks;
    EditorTabs &m_tabs;
    std::shared_ptr<PandaUI::View> m_viewport;
    std::vector<uint32_t> m_worlds;       // the worlds that have a tab
    std::vector<uint32_t> m_closedByUser; // their tabs were closed: they leave the list on the next sync
    uint32_t m_shownWorldId = 0;          // the active world the tabs were last synced to
    uint32_t m_previewWorldId = 0;        // the world of the preview tab (open); 0 — none
    bool m_inSync = false;                // the sync changes the active world itself: no nested sync
    bool m_closingTab = false;            // the sync closes the tab of a world that is gone: not a request of the user
};

} // namespace Editor
