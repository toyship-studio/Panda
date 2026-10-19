#pragma once

#include "Panda/Assets/Base/Path.hpp"

#include <string>
#include <vector>

// Generation of the HLSL declarations of the global UBOs (ShaderBlocks.hpp) from the Rain field
// metadata: `default-shaders/include/panda/<block>.hlsli`. The files are committed (the bake and
// the cross-build need no host tool); they are written by
// `PandaSmoke --write-shader-includes <include root>`, the editor and the smoke test check the
// files against the generation at startup. Rain is only the metadata (name, offset, type,
// element and length of a C array); the cbuffer packing rules and the text are here.
namespace Panda::ShaderInterface {

struct Include {
    std::string fileName; // relative to the include root, e.g. "panda/scene_lights.hlsli"
    std::string text;
};

// Texts of all generated includes. Empty with error filled in — the layout of a C++ struct
// diverges from the cbuffer rules (unsupported type, forgotten alignment): the generation must
// not silently produce a wrong layout.
std::vector<Include> generate(std::string &error);
// Writes the includes to <includeRoot>/<fileName>.
bool writeIncludes(const path_t &includeRoot, std::string &error);
// Freshness: every file in includeRoot is byte-for-byte equal to the generation (line endings
// are normalized). Returns a description of the mismatch; empty — everything is fresh.
std::string checkIncludes(const path_t &includeRoot);

} // namespace Panda::ShaderInterface
