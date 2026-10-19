#pragma once

#include <Foundation/PlatformDetection.hpp>

#ifdef PLATFORM_WINDOWS

#    include <Panda/Assets/Base/Path.hpp>
#    include <Panda/Renderer/ShaderSpirvReflectionData.hpp>

namespace Panda {

// Builds the same reflection data spirv-cross --reflect produces, but straight
// from HLSL via D3DCompile + D3DReflect: MATERIAL_FIELDS members for the
// material inspector and the fragment textures. No external tools involved.
ShaderSpirvReflectionData reflectD3D11FragmentShader(const path_t &fragmentSourcePath);

} // namespace Panda

#endif
