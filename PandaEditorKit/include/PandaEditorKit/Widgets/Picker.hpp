#pragma once

#include <PandaUI/PandaUI.hpp>

#include <functional>
#include <string>
#include <vector>

namespace Editor {

// A row of a picker.
struct PickerItem {
    std::string title;
    std::string detail; // dim, after the title: where the thing is (a folder, the parents of an entity)
    std::string icon;   // an icon of the editor set (Ic::…); empty — none
    std::string hint;   // at the right edge of the row, monospaced: the keys of a command
};

constexpr float PICKER_WIDTH = 340.f;         // a picker next to a field
constexpr float PICKER_PALETTE_WIDTH = 520.f; // a palette in the middle of the window

// A picker — the palette every "choose one of these" of the editor is: a search field over a
// list, shown as a popover at a point of the window. Typing filters the rows (a
// case-insensitive substring of the title or of the detail), the arrows move the selection,
// Enter or a click picks: onPick(the index of the item in items). Escape and a click outside
// close it without a choice.
void showPicker(
    PandaUI::Context *context, PandaUI::Point at, const std::string &placeholder, std::vector<PickerItem> items,
    std::function<void(int)> onPick, float width = PICKER_WIDTH
);
// The same in the middle of the window, under its top edge: a palette that belongs to no field
// (the commands of the editor).
void showPalette(
    PandaUI::Context *context, const std::string &placeholder, std::vector<PickerItem> items,
    std::function<void(int)> onPick
);

} // namespace Editor
