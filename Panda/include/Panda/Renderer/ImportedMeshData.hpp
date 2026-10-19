#pragma once

#include "Panda/Renderer/Vertex.hpp"

namespace Panda {

struct ImportedPrimitiveData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

struct ImportedMeshData {
    std::vector<ImportedPrimitiveData> primitives;
};

} // namespace Panda
