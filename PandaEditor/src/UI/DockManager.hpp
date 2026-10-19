#pragma once

#include "Project/Settings/EditorSettings.hpp" // Panda::DockLayoutState (saving the layout)
#include "UI/Widgets/SplitView.hpp"

#include <PandaUI/PandaUI.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace PandaUI {
class TabView;
}

namespace Editor {

class VisibilityStripView;
class IconButton;

// Docking zones. Center (the viewport) is always present; Left/Right/Bottom are toggleable tabbed docks.
enum class Dock { Left, Right, Bottom, Center };

// A dockable panel: a stable id (groundwork for serialization), the tab title, the icon for the strip,
// the content view and the current dock.
struct DockPanel {
    std::string id;
    std::string title;
    std::string icon;
    std::shared_ptr<PandaUI::View> content;
    Dock dock;
    bool open = true;     // false — the panel is closed (not in the dock; available via the strip and "+")
    bool inStrip = true;  // show the icon in the VisibilityStrip (center scenes — false)
    bool closable = true; // a tab with a close button (center scenes — false)
    // A temporary editor tab of an object (entity/asset): lives in the common specification — rebuild,
    // moving between docks and the menus work as for panels — but it is not saved into the layout, does
    // not get into the strip and "+", and closing REMOVES it (does not switch it to open=false).
    bool transient = false;
    bool preview = false; // unpinned (the title is dimmed); a double click on the tab pins it
};

// Commands from the visibility strip — implemented by DockManager (it is the docking coordinator).
class VisibilityStripDelegate {
public:
    virtual ~VisibilityStripDelegate() = default;
    virtual void stripDidActivatePanel(const std::string &id) = 0;          // LMB: show panel/toggle dock
    virtual void stripDidRequestDock(const std::string &id, Dock dock) = 0; // RMB → Dock X: move
};

// Owns the editor layout and the docking (Zed-like). The Left/Right/Bottom docks are TabViews (a stack of
// panels as tabs; the bar is hidden with a single tab → a lone dock looks like a regular panel). Center is
// the viewport, fixed. The specification (panel→dock + order) lives here — groundwork for serializing
// the positions. maximize is a zoom overlay of a zone (it does not change the visibility of the docks).
// The visibility strip is built from the specification and sends commands here: LMB to show/activate
// a panel (again — to hide the dock), RMB to move it.
class DockManager final : public VisibilityStripDelegate {
public:
    // Status text of the visibility strip (RAM/VRAM/CPU/GPU); EditorLayer calls it once every ~0.5 s.
    void setStripStatusText(const std::string &text);
    DockManager(
        std::shared_ptr<PandaUI::View> toolbar,
        std::shared_ptr<PandaUI::TabView> center, // the viewport (scene tabs); DockManager adds the trailing
        std::vector<DockPanel> panels
    );

    std::shared_ptr<PandaUI::View> rootView() const {
        return m_root;
    }

    bool isVisible(Dock dock) const;          // effective: not hidden by the user AND the dock has panels
    void setVisible(Dock dock, bool visible); // hides/shows the whole dock
    void toggle(Dock dock) {
        setVisible(dock, !isVisible(dock));
    }

    void maximize(Dock dock);                     // zoom overlay of a zone (the same one again — restore)
    void maximizeContaining(PandaUI::View *view); // zooms the dock containing view (buttons in panel headers)
    bool isMaximized() const {
        return m_zoomView != nullptr;
    }
    bool dockContaining(PandaUI::View *view, Dock &out) const;
    // Panel id of the dock's active tab (empty — no selection). Public for undo routing:
    // the active document = the selected tab of the dock that has the focus.
    std::string selectedPanelId(Dock dock) const;

    void setPanelDirty(const std::string &id, bool dirty); // "unsaved" indicator (dot) on the panel's tab

    // --- Temporary editor tabs of objects (transient): the mechanism for the tabbed inspector. ---
    // The policy (which object is in preview, replacement/deduplication) lives in the caller; docking
    // is here. Open: there is no panel with this id — it is added at the end of the dock and selected
    // (WITHOUT moving the focus: the object selection comes from the Hierarchy/viewport, the focus stays
    // there); there is one — it is simply activated, and in its CURRENT dock (the user may have dragged
    // the tab — that is respected).
    void openTransientPanel(DockPanel panel, Dock dock);
    bool activateTransientPanel(const std::string &id); // shows/selects an existing one (false — no such panel)
    void closeTransientPanel(const std::string &id); // removes the tab entirely (the caller's replace policy)
    // Changes the id of a transient tab (the preview shows another object: the id reflects the object,
    // the tab is the same).
    void renameTransientPanel(const std::string &oldId, const std::string &newId);
    // Replaces the tab content IN PLACE (the preview shows another object): without a dock rebuild.
    void updateTransientPanel(
        const std::string &id, std::string title, const std::string &icon,
        std::shared_ptr<PandaUI::View> content
    );
    void setPanelPreview(const std::string &id, bool preview);
    bool panelExists(const std::string &id) const;
    // A tab was closed (close button/menu) — a transient is REMOVED by it; a double click cleared preview.
    void setOnTransientClosed(std::function<void(const std::string &id)> callback);
    void setOnPanelPinned(std::function<void(const std::string &id)> callback);
    // Close gate of a transient tab (unsaved changes): false — the close is cancelled.
    // Does not concern regular panels (they are only hidden, there is nothing to lose).
    void setOnTransientCloseRequest(std::function<bool(const std::string &id)> callback);

    // Extra items of the "+" menu of every dock after the closed panels (the document types of
    // the editor: the settings of the project and of the editor): title + an action that opens the
    // item in the dock whose "+" was clicked.
    struct AddMenuItem {
        std::string title;
        std::function<void(Dock)> open;
    };
    void setAddMenuItemsProvider(std::function<std::vector<AddMenuItem>()> provider);

    // Saving/restoring the layout: panels+order, dock visibility, split weights, active tabs.
    Panda::DockLayoutState captureLayout() const;
    void applyLayout(const Panda::DockLayoutState &state); // an invalid (empty) state is ignored

    // Shows a panel WITHOUT a toggle (navigation/jumps from the UI): a closed one is reopened,
    // a hidden dock is revealed, the tab is selected; an already active one gets the focus.
    void showPanel(const std::string &id);

    // VisibilityStripDelegate.
    void stripDidActivatePanel(const std::string &id) override;
    void stripDidRequestDock(const std::string &id, Dock dock) override;

private:
    void rebuild();      // lays specification panels out into TabView docks + visibility of empty ones + strip
    void refreshStrip(); // builds strip buttons from the specification (grouped by dock, active highlighted)
    void applyDockVisibility(Dock dock);
    bool userVisible(Dock dock) const;
    bool hasPanels(Dock dock) const;
    int tabIndexOf(const std::string &id) const;       // index of the panel's tab within its dock
    DockPanel *findPanel(const std::string &id);
    const DockPanel *findPanel(const std::string &id) const;
    std::shared_ptr<PandaUI::TabView> dockTabView(Dock dock) const;
    std::shared_ptr<PandaUI::View> dockZone(Dock dock) const; // the zone view (for maximize/dockContaining)
    void restoreMaximized();
    void panelClosed(Dock dock, int tabIndex); // the tab was closed with the close button → mark as closed
    // Single point of closing a panel by id: transient — full removal (+ callback), others — open=false.
    // The dock rebuild is done by the caller.
    void closePanelInternal(const std::string &id);
    bool allowClose(const std::string &id); // gate of transient tabs (m_onTransientCloseRequest)
    void tabDoubleClicked(Dock dock, int tabIndex); // pin: clears preview on a transient tab
    // id BY VALUE: it is called from the lambdas of strip/menu buttons, which rebuild→refreshStrip destroys
    // in the course of the call — a reference to the string captured in the lambda would dangle (the
    // "appeared but not selected" bug).
    void reopenPanel(std::string id, Dock dock); // returns a closed panel to a dock (strip / "+")
    // Drag-and-drop of tabs (the common TabView D&D, group "docks"). Stable tab-id ↔ panel id —
    // so that move/reorder find the panel regardless of dock rebuilds.
    void tabReordered(Dock dock, int toIndex);                          // reorder within a dock → syncs m_panels
    void tabMovedToDock(Dock dock, std::uint64_t tabId, int destIndex); // a tab is moved into this dock
    void movePanelTo(const std::string &id, Dock dock, int destIndex);  // repositions a panel in the specification
    std::uint64_t tabIdForPanel(const std::string &id) const;
    std::string panelForTabId(std::uint64_t tabId) const;
    void showAddMenu(Dock dock, PandaUI::View *anchor);         // "+" → menu of closed panels
    void showTabMenu(Dock dock, int tabIndex, PandaUI::Point position); // RMB on a tab: close / move / maximize
    std::shared_ptr<PandaUI::View> makeDockTrailing(Dock dock); // trailing of the tab bar: [+] [fullscreen]
    std::shared_ptr<IconButton> maxButton(Dock dock) const;     // the dock's fullscreen button (icon change)

    std::shared_ptr<PandaUI::Panel> m_root;
    std::shared_ptr<SplitView> m_workspace;    // [Left | centerColumn | Right]
    std::shared_ptr<SplitView> m_centerColumn; // [Center | Bottom]
    std::shared_ptr<PandaUI::TabView> m_leftDock, m_rightDock, m_bottomDock;
    std::shared_ptr<PandaUI::TabView> m_center;                                   // the viewport (fixed)
    std::shared_ptr<IconButton> m_leftMax, m_rightMax, m_bottomMax, m_centerMax; // dock fullscreen buttons
    std::shared_ptr<VisibilityStripView> m_strip;

    std::vector<DockPanel> m_panels;
    std::unordered_map<std::string, std::uint64_t> m_panelTabId; // stable tab-id of a panel (tab moves)
    std::uint64_t m_nextTabId = 1; // continuation of the id space for transient panels
    bool m_leftUserVisible = true, m_rightUserVisible = true, m_bottomUserVisible = true;
    bool m_inRebuild = false; // suppresses refreshStrip from onSelectionChanged during rebuild
    std::function<void(const std::string &)> m_onTransientClosed;
    std::function<void(const std::string &)> m_onPanelPinned;
    std::function<bool(const std::string &)> m_onTransientCloseRequest;
    std::function<std::vector<AddMenuItem>()> m_addMenuItems;

    // maximize (zoom overlay).
    std::shared_ptr<PandaUI::Panel> m_maximizeRoot;
    std::shared_ptr<PandaUI::Panel> m_maximizeHost;
    std::shared_ptr<PandaUI::View> m_zoomView;
    PandaUI::View *m_zoomHome = nullptr;
    Dock m_maximizedDock = Dock::Center;
};

} // namespace Editor
