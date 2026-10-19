#pragma once

#include "PandaUI/Elements/Control.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace PandaUI {

class Label;
class Popup;
class ListView;

// Dropdown list: shows the selected item and a chevron, opens an overlay with the list of
// choices on click. Built on Popup + ListView (selection/hover/scroll are reused).
class Combobox : public Control {
public:
    using SelectionChangedCallback = std::function<void(Combobox &)>;

    Combobox();
    ~Combobox() override;

    void setItems(std::vector<std::string> items);
    void addItem(std::string item);
    void clearItems();
    int itemCount() const {
        return static_cast<int>(m_items.size());
    }

    void setSelectedIndex(int index); // -1 — clear the selection (the placeholder is shown)
    int selectedIndex() const {
        return m_selectedIndex;
    }
    std::string selectedText() const; // current item or placeholder

    void setPlaceholder(std::string text);
    void setOnSelectionChanged(SelectionChangedCallback callback);

    bool keyDown(KeyEvent &event) override; // open: ↑/↓ highlight, Enter — select, Esc — close; closed: ↑/↓ change the selection, Enter — open

private:
    void performPrimaryAction() override;
    void controlStateChanged(ControlState previous, ControlState current) override;
    void themeChanged() override;
    void focusChanged(bool focused) override; // losing focus closes the dropdown; focus border
    void draw(float offsetX, float offsetY) override;

    void openDropdown();
    void moveDropdownHighlight(int delta); // ↑/↓ over the open list (highlight, no commit)
    void commitDropdown();                 // select the highlighted item and close
    void stepSelection(int delta);         // ↑/↓ with the list closed — direct selection change
    void updateLabel();
    void applyStyle();

    std::vector<std::string> m_items;
    int m_selectedIndex = -1;
    std::string m_placeholder;
    std::shared_ptr<Label> m_label;
    SelectionChangedCallback m_onSelectionChanged;
    Popup *m_dropdown = nullptr;       // the open dropdown (or null) — do not open it again
    ListView *m_dropdownList = nullptr; // the list inside the dropdown (for arrow-key navigation)
};

} // namespace PandaUI
