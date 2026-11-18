#pragma once

#include <Panda/Base/UUID.hpp>
#include <Panda/ScriptEngine/ExternalScript.hpp> // ScriptFieldType, ScriptFieldValue
#include <PandaUI/PandaUI.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <variant>

namespace Panda {
class World;
}

namespace Editor {

// The editors of the published fields of a script (PANDA_FIELD) in the form of an entity.

// One value of a script field: the field itself, or an item of a list.
using ScriptItem = std::variant<int32_t, float, Panda::UUID>;

// The value of a plain field as an item, and an item as the value of a plain field.
ScriptItem scriptItemOf(const Panda::ScriptFieldValue &value);
Panda::ScriptFieldValue scriptValueOf(const ScriptItem &item);

// The editor of one value: a number field, an asset well, an entity well. nullptr — nothing
// edits the value (a type that is not a field type, no world for a reference to an entity, an
// asset field whose kind is not known: the scripts are not built). `kind` — of an ASSET field,
// the kind of its assets (ScriptField::kind).
std::shared_ptr<PandaUI::View> makeScriptValueEditor(
    Panda::ScriptFieldType type, const std::string &kind, Panda::World *world, const ScriptItem &value,
    std::function<void(ScriptItem)> set
);

// A list field (a std::vector in the class) and how the form reaches its value.
struct ScriptListField {
    std::string name;             // the field in the class
    Panda::ScriptFieldType type;  // what one item is
    std::string kind;             // of an ASSET list: the kind of its assets
    Panda::World *world = nullptr; // where an entity reference is looked up
    // The value the world holds now. false — the field is gone (the form outlived its target).
    std::function<bool(Panda::ScriptFieldValue &)> read;
    // The new value. structural — items were added, removed or moved: the form is built again
    // after it and the edit is a step of the history of its own.
    std::function<void(Panda::ScriptFieldValue, bool structural)> commit;
};

// The rows of a list field in a section: a header — the name, how many items, the button that
// adds one — and a row per item: its index, the editor of one value, the buttons that move it
// up and remove it.
//
// Several values come at once by a drop on the header: assets of the kind of the list dragged
// from the Content Browser, entities dragged from the Hierarchy — each becomes an item, in the
// order they were dragged in. An item is dragged by its index onto another row and takes the
// place of that row. A drop on the value of an item replaces that value, as on any field.
void addScriptListRows(PandaUI::CollapsibleSection &section, ScriptListField field);

} // namespace Editor
