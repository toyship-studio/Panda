#pragma once

#include "Panda/Assets/Base/Path.hpp"
#include "Panda/Renderer/ShaderFeatures.hpp"

#include <string>
#include <vector>

// Generation of the HLSL side of the engine blocks (ShaderBlocks.hpp) from the Rain field
// metadata: `default-shaders/include/panda/blocks/<block>.hlsli` — one file per block, with the
// feature guard and the system textures of a feature block — and `panda/features.hlsli`, the
// feature macros of the bundle's own shader bake (every feature on). The files are committed (the
// bake and the cross-build need no host tool for them); they are written by
// `PandaSmoke --write-shader-includes <include root>`, the editor and the smoke test check the
// files against the generation at startup. Rain is only the metadata (name, offset, type, element
// and length of a C array); the cbuffer packing rules and the text are here.
namespace Panda::ShaderInterface {

struct Include {
    std::string fileName; // relative to the include root, e.g. "panda/blocks/lights.hlsli"
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
// The feature macros of a set: `#define PANDA_FEATURE_<X> 1|0` per feature. The include expander
// puts this text in front of every shader (the prelude of MirenTools::expandHlslIncludes): the
// editor and the export for the set of the project, the bake for every feature (the committed
// `panda/features.hlsli` is this text for ShaderFeatureSet::all()). The shared includes compile
// only the enabled code, and the block include of a disabled feature fails with a clear error.
std::string featuresInclude(const ShaderFeatureSet &features);

} // namespace Panda::ShaderInterface
