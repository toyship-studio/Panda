#pragma once

#include <Panda/Assets/Base/AssetRef.hpp>
#include <Panda/Base/Base.hpp> // Panda::Color
#include <Panda/Base/UUID.hpp>
#include <Panda/GameLogic/Entity.hpp>
#include <PandaUI/PandaUI.hpp>

#include <functional>
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Panda {
class World;
}

namespace Editor {

// Common bricks of inspector forms (component sections, asset editor tabs) — for the core and
// the editor parts of packages.

// Form section: a caps title without a backing (design: component title).
void styleComponentSection(PandaUI::CollapsibleSection &section);
std::shared_ptr<PandaUI::CollapsibleSection> makeFormSection(const std::string &title);

// uint32_t AssetId (0 = none) → AssetRef<Asset> for writing into engine data.
Panda::AssetRef<Panda::Asset> assetRefOf(uint32_t id);

// Form row button (ButtonSize::Small).
std::shared_ptr<PandaUI::Button>
makeFormButton(const std::string &title, PandaUI::ButtonStyle style, std::function<void()> onClick);

// A row of form buttons across the full section width (the buttons split it equally).
std::shared_ptr<PandaUI::Panel> makeFormButtonRow(std::initializer_list<std::shared_ptr<PandaUI::Button>> buttons);

// The name of a field of code as a form shows it: "baseColorFactor" and "base_color_factor" →
// "Base Color Factor". The fields of a shader and of a script are named by their authors in
// code; the file keeps that name, the label of the row is this (the tooltip says the name).
std::string humanizeIdentifier(const std::string &name);

// Muted form hint (wraps to the section width).
std::shared_ptr<PandaUI::Label> makeFormHint(const std::string &text);

// The editor of a reference to an asset of kind kind (AssetKind::name): AssetWell with drop, a
// picker and clearing; the asset name and selection — through the editor host (EditorHost).
// set(id) — the new value (0 — reset). addAssetRow puts it into a row of a section; on its own it
// is an item of a list.
std::shared_ptr<PandaUI::View> makeAssetWell(const char *kind, uint32_t value, std::function<void(uint32_t)> set);
std::shared_ptr<PandaUI::PropertyRow> addAssetRow(
    PandaUI::CollapsibleSection &section, const char *title, const char *kind, uint32_t value,
    std::function<void(uint32_t)> set, const char *field = nullptr
);

// Typical section rows: a number in a range, a toggle, a color, a choice from a list, a vector,
// a reference to an entity of the world. The callback fires on every change (module sections
// write a copy of the component and commit it with a command). The row comes back for further
// tuning: setHelp (a tooltip over the label), setDescription (a line under it), setVisible (a
// field that does not apply now leaves the form).
//
// `field` — the field of the component the row edits, as the file names it
// (PropertyRow::setField: `intensity`, `camera/fieldOfView`). The form of an entity compares
// the field across what it shows, from the text of the component: the targets differ in it —
// the row says so and the editor shows "—" (showMixed); an instance of a prefab has it of its
// own — the row is marked, its menu offers Revert and Apply. A row of a component names its
// field; a row of a form of something else (the settings, an asset) has none.
//
// A number is typed into its field or scrubbed by dragging the label of the row (PropertyRow).
// A toggle sits at the right edge of the row and its label takes the rest of the width.
std::shared_ptr<PandaUI::PropertyRow> addNumberRow(
    PandaUI::CollapsibleSection &section, const char *title, float min, float max, float value,
    std::function<void(float)> onChange, const char *field = nullptr
);
// A whole number: counts, sizes in texels, ports.
std::shared_ptr<PandaUI::PropertyRow> addIntegerRow(
    PandaUI::CollapsibleSection &section, const char *title, int min, int max, int value,
    std::function<void(int)> onChange, const char *field = nullptr
);
std::shared_ptr<PandaUI::PropertyRow> addToggleRow(
    PandaUI::CollapsibleSection &section, const char *title, bool value, std::function<void(bool)> onChange,
    const char *field = nullptr
);
// alpha — the opacity of the color means something (a sprite, the base color of a material): the
// picker gets a slider for it. Without it the alpha is carried through unchanged.
std::shared_ptr<PandaUI::PropertyRow> addColorRow(
    PandaUI::CollapsibleSection &section, const char *title, const Panda::Color &value,
    std::function<void(const Panda::Color &)> onChange, const char *field = nullptr, bool alpha = false
);
std::shared_ptr<PandaUI::PropertyRow> addComboRow(
    PandaUI::CollapsibleSection &section, const char *title, std::vector<std::string> items, int selected,
    std::function<void(int)> onChange, const char *field = nullptr
);
// X / Y / Z fields (Vector3Field); mixed per axis.
std::shared_ptr<PandaUI::PropertyRow> addVector3Row(
    PandaUI::CollapsibleSection &section, const char *title, glm::vec3 value, std::function<void(glm::vec3)> onChange,
    const char *field = nullptr
);
// A reference to an entity of the world (EntityWell: a picker, a drop from the Hierarchy, a
// clear button).
std::shared_ptr<PandaUI::PropertyRow> addEntityRow(
    PandaUI::CollapsibleSection &section, const char *title, Panda::World &world, Panda::UUID value,
    std::function<void(Panda::UUID)> onChange, const char *field = nullptr
);

// The targets of a form differ in the field of the row: the row says so, and its editor shows
// "—" in place of the value of the primary target (`axes` — per axis, for a vector).
void showMixed(PandaUI::PropertyRow &row, bool mixed, glm::bvec3 axes = glm::bvec3(false));

} // namespace Editor
