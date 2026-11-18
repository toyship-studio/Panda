#pragma once

#include <cstdint>

namespace Panda::Scripting {

using Handle = uint32_t;
using FieldHandle = uint32_t;

// What one value of a script field is. A field holds one value or, when it is a list (a
// std::vector in the class), any number of them. A value crosses the ABI as 4 bytes: an
// int32_t, a float, or the id of an entity or of an asset. v36: MESH — a mesh asset (a model of
// the project). v37: ASSET — an asset of a kind the core has no type of its own for: a kind a
// module brings (an audio clip), named by ScriptFieldInfo::kind. The numbers are written into
// world files: a new type goes before UNKNOWN, the others keep theirs.
enum class ScriptFieldType { INTEGER, FLOAT, ENTITY, TEXTURE, MATERIAL, WORLD, FONT, MESH, ASSET, UNKNOWN };

// v28: the manifest of a script bundle crosses the ABI as flat arrays, like the rest of the ABI.
// The arrays and the strings belong to the bundle and stay valid until the next getManifest call
// or the unload of the bundle; the engine copies them (Panda::ScriptBundleManifest).
struct ScriptFieldInfo {
    FieldHandle handle;
    const char *name;
    ScriptFieldType type;
    bool list; // v34: the field is a list of values of the type
    // v37: of an ASSET field — the name of the kind of its assets (AssetKind::name: "audio");
    // null for every other type.
    const char *kind;
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
