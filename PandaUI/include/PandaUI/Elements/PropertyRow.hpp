#pragma once

#include "PandaUI/Core/Appearance.hpp"

#include "PandaUI/Elements/Panel.hpp"

#include <functional>
#include <memory>
#include <string>

namespace PandaUI {

class Label;
class NumberField;

// Where the editor of a row sits.
enum class PropertyRowPlacement {
    Fill,     // takes the column to the right of the label (fields)
    Leading,  // keeps its own size, at the start of that column (a color well)
    Trailing, // keeps its own size at the right edge of the row; the label takes the rest (a toggle)
};

// The look of a form row: the label column, the row height, the gap and the label text.
struct PropertyRowAppearance {
    // The label column of a row with a filling editor: a share of the width of the row, within
    // limits — a wide form gives its labels room instead of wrapping them, a narrow one keeps
    // the fields usable. labelShare 0 — the column is labelMinWidth wide whatever the form.
    float labelShare = 0.38f;
    float labelMinWidth = 104.f;
    float labelMaxWidth = 200.f;
    float minHeight = 28.f;
    float gap = 12.f;
    Font labelFont = Font(16.f);
    ThemeColor labelColor = ThemeColor::textMuted; // a form field label — muted
    ThemeColor describedLabelColor = ThemeColor::textSecondary; // the title over a description
    Font descriptionFont = Font(13.f);
    ThemeColor descriptionColor = ThemeColor::textDim;
    float descriptionGap = 2.f;       // between the title and the description
    float describedPadding = 5.f;     // above and below a row that has a description
    bool trailingToggles = true;      // a toggle that does not fill sits at the right edge of the row
};

// Property row: a label (left) + an editor (right) — the basis of the inspector and forms.
// With a filling editor the label column is a share of the row (PropertyRowAppearance), the same
// in neighboring rows of one form, which gives an aligned column of fields. fill=false keeps the editor's own size: a toggle goes to the right
// edge of the row and the label takes the rest of it (PropertyRowPlacement::Trailing), anything
// else stays at the start of the field column (Leading). setPlacement names the place directly.
//
// A row whose editor is a NumberField scrubs it: a horizontal drag of the label changes the
// number (NumberField::scrub — Shift is ten times coarser, Alt ten times finer).
class PropertyRow : public Panel {
public:
    PropertyRow(std::string title, std::shared_ptr<View> editor, bool fill = true);
    explicit PropertyRow(std::string title);
    ~PropertyRow() override;

    // The look of the rows created from now on (Appearance.hpp).
    static PropertyRowAppearance &defaultAppearance();
    void setAppearance(const PropertyRowAppearance &appearance);
    const PropertyRowAppearance &appearance() const {
        return m_appearance;
    }

    void setTitle(std::string title);
    const std::string &title() const {
        return m_title;
    }
    void setEditor(std::shared_ptr<View> editor, bool fill = true);
    const std::shared_ptr<View> &editor() const {
        return m_editor;
    }
    void setPlacement(PropertyRowPlacement placement);
    PropertyRowPlacement placement() const {
        return m_placement;
    }
    void setLabelWidth(float width); // a fixed label column for this row
    // Lines of the label: 0 by default — as many as the title needs, the row grows with it; 1 cuts
    // a long title with an ellipsis (a single-height row that must not grow).
    void setLabelNumberOfLines(int numberOfLines);
    // A dim line under the title: what the field does, what it costs. Empty — none. The label of
    // a row with a description wants room: use it with a trailing editor.
    void setDescription(std::string description);
    // What the field is, shown as a tooltip over the label.
    void setHelp(std::string help);
    // Mark that the value differs between the selected objects (multi-selection): a "* " marker
    // is added to the label. The "mixed" state of the editor itself is set on it separately.
    void setMixed(bool mixed);
    // The value is the object's own where it otherwise comes from elsewhere — an instance of a
    // prefab that differs from the prefab here: the label takes the accent color and a bar
    // stands at the start of the row.
    void setOverridden(bool overridden);
    bool isOverridden() const {
        return m_overridden;
    }
    // A right click on the row (its label, the space around the editor): what the form offers
    // for this field. The point is in window coordinates.
    void setOnContextMenu(std::function<void(PropertyRow &, Point)> onContextMenu) {
        m_onContextMenu = std::move(onContextMenu);
    }
    // Shown or taken out of the form with its place (a field that does not apply now).
    void setVisible(bool visible);
    // The label starts this far to the right, the editors stay in their column: a row that
    // belongs to the one above it (the parameters of an effect under its toggle).
    void setIndent(float indent);
    void describeAccessibility(Accessibility &accessibility) const override {
        accessibility.role = "property";
        accessibility.label = m_title;
        if (m_mixed) {
            accessibility.value = "mixed";
        } else if (m_overridden) {
            accessibility.value = "overridden";
        }
    }

    bool pointerDown(PointerEvent &event) override;
    bool pointerMove(PointerEvent &event) override;
    bool pointerUp(PointerEvent &event) override;
    void interactionCancelled() override;

private:
    void refreshLabel();
    void applyAppearance();
    void applyPlacement();
    bool overLabel(Point windowPosition) const;

    PropertyRowAppearance m_appearance;
    std::shared_ptr<Panel> m_labelColumn; // the title and the description under it
    std::shared_ptr<Label> m_label;
    std::shared_ptr<Label> m_description;
    std::shared_ptr<View> m_editor;
    NumberField *m_scrubField = nullptr; // the editor, when it is a number field
    std::string m_title;
    PropertyRowPlacement m_placement = PropertyRowPlacement::Fill;
    float m_scrubX = 0.f;
    bool m_scrubbing = false;
    bool m_mixed = false;
    bool m_overridden = false;
    std::shared_ptr<Panel> m_overrideBar; // made when the row is first overridden
    std::function<void(PropertyRow &, Point)> m_onContextMenu;
};

} // namespace PandaUI
