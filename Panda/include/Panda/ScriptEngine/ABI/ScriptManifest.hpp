#pragma once

#include <cstdint>

namespace Panda::Scripting {

using Handle = uint32_t;
using FieldHandle = uint32_t;

enum class ScriptFieldType { INTEGER, FLOAT, ENTITY, TEXTURE, MATERIAL, WORLD, UNKNOWN };

// v28: the manifest of a script bundle crosses the ABI as flat arrays, like the rest of the ABI.
// The arrays and the strings belong to the bundle and stay valid until the next getManifest call
// or the unload of the bundle; the engine copies them (Panda::ScriptBundleManifest).
struct ScriptFieldInfo {
    FieldHandle handle;
    const char *name;
    ScriptFieldType type;
};

struct ScriptClassInfo {
    const char *name;
    int32_t order;
    const ScriptFieldInfo *fields;
    uint32_t fieldCount;
};

struct ScriptBundleInfo {
    const ScriptClassInfo *classes;
    uint32_t classCount;
};

} // namespace Panda::Scripting

namespace Panda {

using Handle = Scripting::Handle;
using FieldHandle = Scripting::FieldHandle;
using ScriptFieldType = Scripting::ScriptFieldType;

} // namespace Panda
