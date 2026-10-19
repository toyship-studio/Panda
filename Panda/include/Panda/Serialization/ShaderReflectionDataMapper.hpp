#pragma once

#include "Panda/Renderer/MaterialData.hpp"
#include "Panda/Renderer/ShaderSpirvReflectionData.hpp"

namespace Panda {

/// Transfers data from ShaderSpirvReflectionData to MaterialData
class ShaderReflectionDataMapper final {
public:
    static MaterialData toMaterialData(const ShaderSpirvReflectionData &reflection);
};

} // namespace Panda