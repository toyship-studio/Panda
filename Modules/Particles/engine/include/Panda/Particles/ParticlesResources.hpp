#pragma once

#include "Panda/Assets/MaterialAsset.hpp"
#include "Panda/Assets/ShaderAsset.hpp"
#include "Panda/Extensions/ResourceProviders.hpp"
#include "Panda/Renderer/RuntimeMesh.hpp"

#include <memory>

namespace Panda::Particles::Resources {

    // Static resources of the module (ResourceProvider): id — the external modules section 0xE…
    // (outside the core sequence and the random ids of projects).
    extern AssetRef<ShaderAsset> shader;     // quad from a mat4 instance + instance color
    extern AssetRef<MaterialAsset> material; // alpha-blend, double-sided; system feeds the texture
    extern std::unique_ptr<RuntimeMesh> quadMesh;     // unit quad in the common Vertex layout
    extern Miren::VertexLayoutHandle instanceLayout;  // 4×vec4 matrix + vec4 color

    ResourceProvider provider();

} // namespace Panda::Particles::Resources
