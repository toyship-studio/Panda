#pragma once

#include "Panda/Assets/MaterialAsset.hpp"
#include "Panda/Extensions/ResourceProviders.hpp"
#include "Panda/Assets/ShaderAsset.hpp"
#include "Panda/Assets/TextureAsset.hpp"

namespace Panda {

namespace Terrain {

    // Static resources of the terrain contribution (ResourceProvider): created after the core
    // (its placeholder textures are needed), the ids are historical core slots reserved in
    // StaticResources (they are persisted in meta.shader of project materials).
    namespace Resources {

        // Defaults of the Terrain2D/3D components (an empty material reference in the component).
        extern AssetRef<ShaderAsset> shader2D;     // blend of 4 layers by splatmap
        extern AssetRef<MaterialAsset> material2D;
        extern AssetRef<ShaderAsset> shader3D;     // blend of 4 layers by vertex weights
        extern AssetRef<MaterialAsset> material3D;
        extern AssetRef<ShaderAsset> idsShader;    // IDS pass of the thin terrain vertex
        extern AssetRef<MaterialAsset> idsMaterial;
        extern AssetRef<ShaderAsset> shadowDepthShader; // depth pass of the shadow cascades
        extern Miren::VertexLayoutHandle vertexLayout;  // TerrainVertex (heightmap 2d)
        // Terrain grass (Tuft tufts): a tiling fbm noise (R — wind gusts, G — patches,
        // B — spare), the blade shader and the material that carries the fields (the palette
        // comes from FloraSettings).
        extern AssetRef<TextureAsset> noiseTexture;
        extern AssetRef<ShaderAsset> grassShader;
        extern AssetRef<MaterialAsset> grassMaterial;

        ResourceProvider provider();

    } // namespace Resources

} // namespace Terrain

} // namespace Panda
