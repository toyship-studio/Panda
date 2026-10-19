#pragma once

#include "PandaUI/Core/TextureHandle.hpp"
#include "PandaUI/Elements/Panel.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace PandaUI {

class View;
class Label;

// Tabbed container: a row of headers + the content area of the selected tab. Headers are dragged via the
// shared drag-and-drop: reorder within its own bar with a vertical indicator line, and, when a drag group
// is set, transfer between different TabViews (docking). A tab has an "unsaved" indicator (a dot). The
// content of every tab always stays alive and is switched by visibility.
class TabView : public Panel {
public:
    using SelectionChangedCallback = std::function<void(TabView &)>;
    using ReorderCallback = std::function<void(TabView &, int from, int to)>;
    using TabClosedCallback = std::function<void(TabView &, int index)>;
    // A tab was moved FROM another TabView (of the same group) into this one, to position destIndex; tabId
    // is its id in source. Applying the move (transferring the data/panel) is up to the application.
    using TabMovedCallback = std::function<void(TabView &source, std::uint64_t tabId, int destIndex)>;
    using TabContextMenuCallback = std::function<void(TabView &, int index, Point windowPosition)>; // tab right-click
    using TabDoubleClickCallback = std::function<void(TabView &, int index)>; // double-click (pins the preview)
    // Gate for closing via the close button: false — the close is cancelled (unsaved changes, etc.).
    using TabCloseRequestCallback = std::function<bool(TabView &, int index)>;

    TabView();
    ~TabView() override;

    // closable=true → the tab has a close button. id — a stable identifier (for transfer between
    // bars); 0 → auto id. draggable=false — the tab cannot be dragged (pinned, e.g. the viewport).
    // icon (optional, valid handle) — a tintable icon to the left of the title (docking: the panel icon
    // from the strip).
    int addTab(
        std::string title, std::shared_ptr<View> content, bool closable = true, std::uint64_t id = 0,
        bool draggable = true, TextureHandle icon = {}
    );

    void setSelectedIndex(int index);
    int selectedIndex() const {
        return m_selectedIndex;
    }
    void focusSelectedContent(); // focuses the first focusable view of the selected tab's content
    void setTabTitle(int index, std::string title);
    void setTabDirty(int index, bool dirty); // "unsaved" indicator (a dot next to the title)
    // Preview tab (unpinned, the VS Code/Zed model): the title is in italics (a synthetic slant done
    // by the renderer — the font set has no italic faces). Clearing/setting it is application policy
    // (a double-click usually pins: setOnTabDoubleClicked → setTabPreview(false)).
    void setTabPreview(int index, bool preview);
    bool tabPreview(int index) const;
    void setTabIcon(int index, TextureHandle icon); // change/remove (invalid) the tab icon
    // Replaces the tab content IN PLACE (a preview tab shows a different object): the old content is
    // detached from the hierarchy and returned; the tab/selection/order are left untouched.
    std::shared_ptr<View> replaceTabContent(int index, std::shared_ptr<View> content);
    int tabCount() const {
        return static_cast<int>(m_tabs.size());
    }
    std::uint64_t tabId(int index) const; // stable id of tab index (0 if out of range)

    void setOnSelectionChanged(SelectionChangedCallback callback);
    void setOnReorder(ReorderCallback callback);     // (from, to) after a reorder within this bar
    void setOnTabClosed(TabClosedCallback callback); // (index) after a tab is closed via the close button
    void setOnTabMoved(TabMovedCallback callback);   // a tab was moved in from another bar (of the same group)
    void setOnTabContextMenu(TabContextMenuCallback callback); // right-click on a tab (index, window position)
    void setOnTabDoubleClicked(TabDoubleClickCallback callback); // double-click on a tab
    void setOnTabCloseRequest(TabCloseRequestCallback callback); // gate BEFORE closing via the close button

    // Right accessory in the tab bar (pinned to the right edge) — e.g. the "+"/maximize buttons. Survives rebuild.
    void setTrailingAccessory(std::shared_ptr<View> accessory);

    // Removes tab index and returns its content (for docking — transfer to another TabView). onClose is NOT called.
    std::shared_ptr<View> removeTab(int index);
    void removeAllTabs();
    // Hides the tab bar when there is a single tab (a lone dock looks like a panel without a bar — as in the design).
    void setHidesTabBarWhenSingle(bool hide);
    // Docking group: tab transfer is allowed between TabViews with the same NON-empty group. Empty by
    // default — no transfer between bars, only reorder within itself.
    void setTabDragGroup(std::string group);

    bool canAcceptDrop(const DragInfo &drag) override; // accepts "tabview.tab" from itself or from its group
    void dragUpdated(const DragInfo &drag) override;   // indicator line of the insertion position
    void dragExited() override;
    bool performDrop(const DragInfo &drag) override; // reorder (own bar) or transfer (another bar of the group)

protected:
    void themeChanged() override;

private:
    class Tab;
    friend class Tab;

    struct TabItem {
        std::shared_ptr<Tab> control;
        std::shared_ptr<View> content;
        std::uint64_t id = 0;
    };

    void selectTab(Tab *tab);
    void closeTab(Tab *tab);
    void tabContextMenu(Tab *tab, Point windowPosition); // right-click on a tab → callback with the index
    void tabDoubleClicked(Tab *tab);                     // double-click → callback with the index
    int indexOfTab(Tab *tab) const;
    int indexOfTabId(std::uint64_t id) const;
    int computeInsertIndex(float windowX) const; // insertion position from the cursor X (0..tabCount)
    void showDropLine(int insertIndex);           // show/reposition the vertical line
    void hideDropLine();
    void rebuildTabBar();         // rearrange the headers in the bar in the current m_tabs order
    void applySelection();        // highlight of the selected tab
    void applyContent();          // show the content of the selected tab, hide the rest
    void applyTabBarVisibility(); // hide/show the bar based on m_hidesTabBarWhenSingle and the tab count

    void viewDidCalculateLayout() override; // deferred scroll to the active tab (frames are ready)

    std::shared_ptr<Panel> m_tabBar;
    std::shared_ptr<class ScrollView> m_tabsScroll; // the tabs scroll, the accessory is pinned on the right
    bool m_pendingRevealSelected = false;
    std::shared_ptr<Panel> m_contentArea;
    std::shared_ptr<View> m_trailingAccessory; // right accessory of the tab bar; null — none
    std::shared_ptr<Panel> m_dropLine;          // vertical indicator line of the insertion position during a drag
    bool m_hidesTabBarWhenSingle = false;
    std::vector<TabItem> m_tabs;
    int m_selectedIndex = -1;
    int m_dropIndex = -1;          // insertion position under the cursor during drag-over
    std::string m_dragGroup;       // docking group (transfer between bars); empty — reorder only
    std::uint64_t m_nextTabId = 1; // counter of auto tab ids
    SelectionChangedCallback m_onSelectionChanged;
    ReorderCallback m_onReorder;
    TabClosedCallback m_onClose;
    TabMovedCallback m_onTabMoved;
    TabContextMenuCallback m_onTabContextMenu;
    TabDoubleClickCallback m_onTabDoubleClicked;
    TabCloseRequestCallback m_onTabCloseRequest;
};

} // namespace PandaUI
