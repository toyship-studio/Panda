#pragma once

#include "Bamboo/Allocator.hpp"
#include "Bamboo/Script.hpp"
#include "Panda/ScriptEngine/ABI/ScriptManifest.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Bamboo {

using InstantiateScriptFunction = Bamboo::Shared<Bamboo::Script> (*)(Bamboo::EntityHandle entity);
using ScriptFieldType = Panda::ScriptFieldType;
using FieldHandle = Panda::FieldHandle;
using Handle = Panda::Handle;
using ScriptID = Panda::Handle;

// Writes the values the engine holds for a field into the field of an instance: `values` is an
// array of `count` 4-byte items (Panda::Scripting::SetFieldValueFn).
using AssignScriptFieldFunction = void (*)(void *field, const void *values, uint32_t count);

struct ScriptFieldInfo final {
    constexpr ScriptFieldInfo(
        ScriptFieldType type, bool list, const char *name, uint32_t offset, AssignScriptFieldFunction assign,
        const char *kind = nullptr
    )
        : type(type)
        , list(list)
        , kind(kind)
        , name(name)
        , offset(offset)
        , assign(assign) {}

    ScriptFieldType type; // what one value is
    bool list;            // the field is a std::vector of them
    const char *kind;     // of an ASSET field: the kind of its assets (the handle's ASSET_KIND); null otherwise
    const char *name;
    uint32_t offset;
    AssignScriptFieldFunction assign; // null for a type that is not a field type (UNKNOWN)
};

struct ScriptFieldDefinition final {
    constexpr ScriptFieldDefinition(FieldHandle handle, ScriptFieldInfo info)
        : handle(handle)
        , info(info) {}

    FieldHandle handle;
    ScriptFieldInfo info;
};

struct ScriptClass final {
    const char *name;
    int order = 0;
    InstantiateScriptFunction instantiateFunc;
    std::vector<ScriptFieldDefinition> fields;
};

} // namespace Bamboo
