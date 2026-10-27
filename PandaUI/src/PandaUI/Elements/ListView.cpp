#include "PandaUI/Elements/ListView.hpp"

#include "PandaUI/Core/KeyboardEvent.hpp"
#include "PandaUI/Core/Theme.hpp"
#include "PandaUI/Elements/Control.hpp"
#include "PandaUI/Elements/Label.hpp"

#include <algorithm>
#include <utility>

namespace PandaUI {

// A single list row: a container Control with selection/hover shown through the background. For text
// rows it keeps a pointer to the Label in order to recolor the text in the selected state.
class ListView::ListRow : public Control {
public:
    ListRow(ListView *owner, int index, std::shared_ptr<View> content, std::shared_ptr<Label> label)
        : m_owner(owner)
        , m_index(index)
        , m_label(label.get()) {
        setCursor(Cursor::PointingHand);
        setFocusable(false); // the list itself holds the focus, not the rows (keys go to the list)
        layout().setFlexDirection(FlexDirection::Row);
        layout().setAlignItems(Align::Center);
        addSubview(std::move(content));
        applyAppearance();
    }

    void applyAppearance() {
        const ListViewAppearance &a = m_owner->appearance();
        layout().setHeight(Length::points(a.rowHeight));
        layout().setPadding(Edge::Horizontal, a.rowPaddingX);
        surface().setCornerRadius(a.rowRadius);
        if (m_label) { m_label->setFont(a.font); }
        applyStyle();
    }

    void setRowSelected(bool selected, bool active) {
        if (m_selected == selected && m_active == active) { return; }
        m_selected = selected;
        m_active = active; // active = the list is focused → brighter selection
        applyStyle();
    }

protected:
    bool pointerDown(PointerEvent &event) override {
        // The "multiple selection" modifier (⌘ on macOS / Ctrl on Win/Linux) is remembered on
        // press: performPrimaryAction (on release) no longer receives the event.
        m_additiveClick = event.isSuperDown || event.isControlDown;
        if (m_owner->m_commitsOnPointerDown) {
            // Lists in auto-closing popovers commit on press: the anchor blur has already started the overlay
            // teardown, and the row will not survive until release (see setCommitsSelectionOnPointerDown).
            m_owner->handleRowClicked(m_index, m_additiveClick);
            return true;
        }
        return Control::pointerDown(event);
    }
    void performPrimaryAction() override {
        m_owner->handleRowClicked(m_index, m_additiveClick);
    }
    void controlStateChanged(ControlState, ControlState) override {
        applyStyle();
    }
    void themeChanged() override {
        applyStyle();
    }

private:
    void applyStyle() {
        const ListViewAppearance &a = m_owner->appearance();
        if (m_selected) {
            setBackgroundColor(m_active ? a.selection : a.selectionInactive); // soft; brighter in focus
        } else if (hasControlState(ControlState::Hovered) || hasControlState(ControlState::Highlighted)) {
            setBackgroundColor(a.hover);
        } else {
            setBackgroundColor(Color(0x00000000));
        }
        if (m_selected && a.selectionImage) {
            surface().setBackgroundImage(a.selectionImage);
        } else {
            surface().clearBackgroundImage();
        }
        if (m_label) { m_label->setTextColor(a.text); } // the text stays regular — the soft plate is readable
    }

    ListView *m_owner;
    int m_index;
    Label *m_label;
    bool m_selected = false;
    bool m_active = false;        // the list is focused (for the active selection color)
    bool m_additiveClick = false; // the ⌘/Ctrl modifier remembered on press
};

ListViewAppearance &ListView::defaultAppearance() {
    static ListViewAppearance appearance;
    return appearance;
}

ListView::ListView()
    : m_appearance(defaultAppearance())
    , m_selectionMode(ListSelectionMode::Single)
    , m_onSelectionChanged(nullptr) {
    setScrollDirection(ScrollDirection::Vertical);
    setFocusable(true); // the list accepts focus → the arrow keys and the focus ring work
    getContentView()->layout().setFlexDirection(FlexDirection::Column);
}

ListView::~ListView() = default;

void ListView::setAppearance(const ListViewAppearance &appearance) {
    m_appearance = appearance;
    for (ListRow *row : m_rows) { row->applyAppearance(); }
}

int ListView::addItem(std::string text) {
    auto label = std::make_shared<Label>(std::move(text));
    label->setFont(m_appearance.font);
    return addRowInternal(label, label);
}

int ListView::addRow(std::shared_ptr<View> content) {
    return addRowInternal(std::move(content), nullptr);
}

int ListView::addRowInternal(std::shared_ptr<View> content, std::shared_ptr<Label> label) {
    const int index = static_cast<int>(m_rows.size());
    auto row = std::make_shared<ListRow>(this, index, std::move(content), std::move(label));
    m_rows.push_back(row.get());
    addContentSubview(row);
    return index;
}

void ListView::clearItems() {
    getContentView()->removeAllSubviews();
    m_rows.clear();
    m_selection.clear();
}

void ListView::setSelectionMode(ListSelectionMode mode) {
    if (m_selectionMode == mode) { return; }
    m_selectionMode = mode;
    if (mode == ListSelectionMode::None) {
        m_selection.clear();
        applySelectionStyles();
    } else if (mode == ListSelectionMode::Single && m_selection.size() > 1) {
        m_selection.resize(1);
        applySelectionStyles();
    }
}

void ListView::setSelectedIndex(int index) {
    m_selection.clear();
    if (index >= 0 && index < rowCount()) { m_selection.push_back(index); }
    applySelectionStyles();
}

int ListView::selectedIndex() const {
    return m_selection.empty() ? -1 : m_selection.front();
}

bool ListView::isSelected(int index) const {
    return std::find(m_selection.begin(), m_selection.end(), index) != m_selection.end();
}

void ListView::setOnSelectionChanged(SelectionChangedCallback callback) {
    m_onSelectionChanged = std::move(callback);
}

void ListView::setCommitsSelectionOnPointerDown(bool commits) {
    m_commitsOnPointerDown = commits;
}

void ListView::handleRowClicked(int index, bool additive) {
    if (m_selectionMode == ListSelectionMode::None) { return; }

    if (m_selectionMode == ListSelectionMode::Multiple && additive) {
        // ⌘/Ctrl-click — add the row to the selection or remove it from the selection.
        auto it = std::find(m_selection.begin(), m_selection.end(), index);
        if (it != m_selection.end()) {
            m_selection.erase(it);
        } else {
            m_selection.push_back(index);
        }
    } else {
        // Plain click (or Single mode) — select exactly one row.
        if (m_selection.size() == 1 && m_selection.front() == index) { return; }
        m_selection.assign(1, index);
    }
    applySelectionStyles();
    if (m_onSelectionChanged) { m_onSelectionChanged(*this); }
}

void ListView::applySelectionStyles() {
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) { m_rows[i]->setRowSelected(isSelected(i), isFocused()); }
}

void ListView::focusChanged(bool) {
    applySelectionStyles(); // active/inactive selection color
}

bool ListView::keyDown(KeyEvent &event) {
    if (m_selectionMode == ListSelectionMode::None || rowCount() == 0) { return false; }
    if (event.key == Key::Up) {
        moveSelection(-1);
        return true;
    }
    if (event.key == Key::Down) {
        moveSelection(1);
        return true;
    }
    return false;
}

void ListView::moveSelection(int delta) {
    if (m_selectionMode == ListSelectionMode::None || rowCount() == 0) { return; }
    int current = selectedIndex();
    if (current < 0) { current = delta > 0 ? -1 : rowCount(); } // first step → 0 or the last row
    const int next = std::clamp(current + delta, 0, rowCount() - 1);
    scrollIndexIntoView(next);
    if (m_selection.size() == 1 && m_selection.front() == next) { return; } // already selected
    m_selection.assign(1, next);
    applySelectionStyles();
    if (m_onSelectionChanged) { m_onSelectionChanged(*this); }
}

void ListView::scrollIndexIntoView(int index) {
    if (index < 0 || index >= rowCount()) { return; }
    const Rect rowFrame = m_rows[index]->getFrame(); // in contentView coordinates
    const float rowTop = rowFrame.origin.y;
    const float rowBottom = rowTop + rowFrame.size.height;
    const float viewTop = getContentOffset().y;
    const float viewHeight = getFrame().size.height;
    if (rowTop < viewTop) {
        setContentOffset(Point(0.f, rowTop));
    } else if (rowBottom > viewTop + viewHeight) {
        setContentOffset(Point(0.f, rowBottom - viewHeight));
    }
}

} // namespace PandaUI
