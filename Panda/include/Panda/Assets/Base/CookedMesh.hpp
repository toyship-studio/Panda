#pragma once

#include "Panda/Renderer/ImportedMeshData.hpp"

#include <istream>
#include <ostream>
#include <vector>

namespace Panda {

// The geometry of a model as a game loads it (.pmesh): the vertices and the indices of every
// primitive of every mesh of the model, as the renderer takes them. The editor reads the glTF
// once, when it cooks the game; the game reads arrays — no JSON, no accessors, no source file of
// the artist in the build.
namespace CookedMesh {

    void write(std::ostream &stream, const std::vector<ImportedMeshData> &meshes);
    // false — not a cooked mesh of this version (or the vertex of the engine has changed since).
    bool read(std::istream &stream, std::vector<ImportedMeshData> &meshes);

} // namespace CookedMesh

} // namespace Panda
