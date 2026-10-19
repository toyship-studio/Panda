#pragma once

#include "Bamboo/Allocator.hpp"
#include "Bamboo/Script.hpp"
#include "Panda/ScriptEngine/ABI/ScriptManifest.hpp"

#include <string>
#include <vector>

namespace Bamboo {

using InstantiateScriptFunction = Bamboo::Shared<Bamboo::Script> (*)(Bamboo::EntityHandle entity);
using ScriptFieldType = Panda::ScriptFieldType;
using FieldHandle = Panda::FieldHandle;
using Handle = Panda::Handle;
using ScriptID = Panda::Handle;

struct ScriptFieldInfo final {
    constexpr ScriptFieldInfo(ScriptFieldType type, const char *name, uint32_t offset, uint32_t size, uint32_t align)
        : type(type)
        , name(name)
        , offset(offset)
        , size(size)
        , align(align) {}

    ScriptFieldType type;
    const char *name;
    uint32_t offset;
    uint32_t size;
    uint32_t align;
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
