#pragma once

#include "PandaUI/Core/Appearance.hpp"

#include "PandaUI/Elements/ScrollView.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace PandaUI {

enum class ListSelectionMode {
    None,     // rows are not selected
    Single,   // at most one selected row
    Multiple, // a click toggles the row in the selected set
};

// Vertical scrollable list of selectable rows. The rows are ordinary Views in the tree
// (no virtualization): a simple and predictable retained mode. Text rows are created
// via addItem(), arbitrary ones via addRow().
// The look of a list: the rows, their text and the selection and hover plates.
struct ListViewAppearance {
    float rowHeight = 30.f;
    float rowPaddingX = Spacing::md;
    float rowRadius = 0.f; // row selection — no rounding
    Font font = Font(16.f);
    ThemeColor text = ThemeColor::textPrimary; // the text stays regular on the soft selection
    ThemeColor selection = ThemeColor::selection; // when the list is focused
    ThemeColor selectionInactive = ThemeColor::selectionInactive;
    ThemeColor hover = ThemeColor::rowHover;
    SurfaceImage selectionImage;
};

class ListView : public ScrollView {
public:
    static constexpr float kRowHeight = 30.f; // the row height of the default appearance

    using SelectionChangedCallback = std::function<void(ListView &)>;
    using ItemActivatedCallback = std::function<void(ListView &, int index)>;

    ListView();
    ~ListView() override;

    // The look of the lists created from now on (Appearance.hpp).
    static ListViewAppearance &defaultAppearance();
    void setAppearance(const ListViewAppearance &appearance);
    const ListViewAppearance &appearance() const {
        return m_appearance;
    }

    int addItem(std::string text);
    int addRow(std::shared_ptr<View> content);
    void clearItems();
    int rowCount() const {
        return static_cast<int>(m_rows.size());
    }

    void setSelectionMode(ListSelectionMode mode);
    ListSelectionMode selectionMode() const {
        return m_selectionMode;
    }

    void setSelectedIndex(int index); // -1 — clear the selection
    int selectedIndex() const;        // first selected or -1
    const std::vector<int> &selectedIndices() const {
        return m_selection;
    }
    bool isSelected(int index) const;

    void setOnSelectionChanged(SelectionChangedCallback callback);
    // A row was chosen, not just walked over: a click on it (also on the selected one) or
    // activateSelected. A palette picks on this and lets the arrows move the selection freely.
    void setOnItemActivated(ItemActivatedCallback callback);
    // Activates the selected row (Enter of a palette); false — nothing is selected.
    bool activateSelected();
    // The selection moved as the arrow keys move it: for a field that owns the keyboard next to
    // the list.
    void moveSelection(int delta);

    // Commit the selection on PRESS, not on release. For lists inside popovers that close
    // (Combobox dropdown): the anchor's blur on pointerDown starts tearing the popover down, and the
    // release never reaches the row (the row is destroyed together with the overlay between down
    // and up).
    void setCommitsSelectionOnPointerDown(bool commits);

    // Scroll so that the row at index is visible (for external navigation, e.g. Combobox dropdown).
    void scrollIndexIntoView(int index);

    bool keyDown(KeyEvent &event) override; // ↑/↓ — change the selection (when the list is focused)
    void describeAccessibility(Accessibility &accessibility) const override {
        accessibility.role = "list";
    }

protected:
    void focusChanged(bool focused) override; // active/inactive selection color

private:
    class ListRow;

    int addRowInternal(std::shared_ptr<View> content, std::shared_ptr<class Label> label);
    void handleRowClicked(int index, bool additive);
    void applySelectionStyles();

    ListViewAppearance m_appearance;
    std::vector<ListRow *> m_rows; // ownership is in contentView; non-owning pointers here
    std::vector<int> m_selection;
    ListSelectionMode m_selectionMode;
    SelectionChangedCallback m_onSelectionChanged;
    ItemActivatedCallback m_onItemActivated;
    bool m_commitsOnPointerDown = false; // see setCommitsSelectionOnPointerDown
};

} // namespace PandaUI
