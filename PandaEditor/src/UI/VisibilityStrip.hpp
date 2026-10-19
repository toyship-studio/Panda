#pragma once

#include "UI/DockManager.hpp" // Dock + VisibilityStripDelegate
#include <PandaEditorKit/Widgets/Icons.hpp>        // IconButton

#include <PandaUI/PandaUI.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Editor {

// Strip item: a panel (id + icon) in its dock + whether it is active (visible and its tab is selected).
struct StripItem {
    std::string id;
    std::string icon;
    Dock dock;
    bool open;   // the tab exists (open) or is closed (dim, a click opens it in its dock)
    bool active; // open, visible and its tab is selected → highlight
};

// Bottom visibility strip (design §5) — the "dock control center". The buttons are built from the
// docking specification: on the left — panels of the left dock; on the right — of the bottom one,
// then the right one (Inspector outermost). The active panel is highlighted (accentSoft). LMB —
// show/activate the panel (again — hide the dock); RMB — the Dock Left/Right/Bottom menu (move).
// Commands go to the delegate (DockManager).
class VisibilityStripView final : public PandaUI::Panel {
public:
    explicit VisibilityStripView(VisibilityStripDelegate *delegate);

    void setItems(const std::vector<StripItem> &items); // rebuild the buttons from the specification
    // Status indicators on the right (Zed style): RAM/VRAM/CPU/GPU. Updated by the owner
    // once every ~half a second; the strip only displays.
    void setStatusText(const std::string &text);

private:
    std::shared_ptr<IconButton> makeButton(const StripItem &item);
    void showDockMenu(const std::string &id, PandaUI::View *anchor); // RMB → Dock Left/Right/Bottom

    VisibilityStripDelegate *m_delegate;
    std::shared_ptr<PandaUI::Panel> m_leftGroup;
    std::shared_ptr<PandaUI::Panel> m_rightGroup;
    std::shared_ptr<PandaUI::Label> m_statusLabel;
};

} // namespace Editor
