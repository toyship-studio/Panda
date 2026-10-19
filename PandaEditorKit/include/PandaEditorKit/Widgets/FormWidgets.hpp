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

// Muted form hint (wraps to the section width).
std::shared_ptr<PandaUI::Label> makeFormHint(const std::string &text);

// A row referencing an asset of kind kind (AssetKind::name): AssetWell with drop, a picker and
// clearing; the asset name and selection — through the editor host (EditorHost). set(id) — the new
// value (0 — reset).
void addAssetRow(
    PandaUI::CollapsibleSection &section, const char *title, const char *kind, uint32_t value,
    std::function<void(uint32_t)> set
);

// Typical section rows: a number in a range, a toggle, a color, a choice from a list, a vector,
// a reference to an entity of the world. The callback fires on every change (module sections
// write a copy of the component and commit it with a command). mixed — the targets of the form
// differ in this field: the row says so and the widget shows "—" instead of the value (the
// primary target's) until the user sets one. The row comes back for further tuning.
std::shared_ptr<PandaUI::PropertyRow> addNumberRow(
    PandaUI::CollapsibleSection &section, const char *title, float min, float max, float value,
    std::function<void(float)> onChange, bool mixed = false
);
std::shared_ptr<PandaUI::PropertyRow> addToggleRow(
    PandaUI::CollapsibleSection &section, const char *title, bool value, std::function<void(bool)> onChange,
    bool mixed = false
);
std::shared_ptr<PandaUI::PropertyRow> addColorRow(
    PandaUI::CollapsibleSection &section, const char *title, const Panda::Color &value,
    std::function<void(const Panda::Color &)> onChange, bool mixed = false
);
std::shared_ptr<PandaUI::PropertyRow> addComboRow(
    PandaUI::CollapsibleSection &section, const char *title, std::vector<std::string> items, int selected,
    std::function<void(int)> onChange, bool mixed = false
);
// X / Y / Z fields (Vector3Field); mixed — per axis.
std::shared_ptr<PandaUI::PropertyRow> addVector3Row(
    PandaUI::CollapsibleSection &section, const char *title, glm::vec3 value, std::function<void(glm::vec3)> onChange,
    glm::bvec3 mixed = glm::bvec3(false)
);
// A reference to an entity of the world (EntityWell: drop from the Hierarchy, a click clears).
std::shared_ptr<PandaUI::PropertyRow> addEntityRow(
    PandaUI::CollapsibleSection &section, const char *title, Panda::World &world, Panda::UUID value,
    std::function<void(Panda::UUID)> onChange, bool mixed = false
);

// Whether a field of component T differs among the targets of a form that have T — the mixed
// flag of its row. get(T &) → the field value (anything comparable with ==).
template<typename T, typename Get>
bool componentFieldMixed(const std::vector<Panda::Entity> &entities, Get get) {
    bool found = false;
    decltype(get(std::declval<T &>())) first{};
    for (Panda::Entity entity : entities) {
        if (!entity.hasComponent<T>()) { continue; }
        auto value = get(entity.getComponent<T>());
        if (!found) {
            first = value;
            found = true;
        } else if (!(value == first)) {
            return true;
        }
    }
    return false;
}
// The same for a vector field, per axis.
template<typename T, typename Get>
glm::bvec3 componentVectorMixed(const std::vector<Panda::Entity> &entities, Get get) {
    return glm::bvec3(
        componentFieldMixed<T>(entities, [&get](T &c) { return get(c).x; }),
        componentFieldMixed<T>(entities, [&get](T &c) { return get(c).y; }),
        componentFieldMixed<T>(entities, [&get](T &c) { return get(c).z; })
    );
}

} // namespace Editor
