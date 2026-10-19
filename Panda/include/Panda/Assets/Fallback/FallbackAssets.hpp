#pragma once

#include "Panda/Assets/MaterialAsset.hpp"
#include "Panda/Assets/ImportedMeshAsset.hpp"

namespace Panda {

class FallbackTextureAsset final : public TextureAsset {
public:
    FallbackTextureAsset();
};

class FallbackMaterialAsset final : public MaterialAsset {
public:
    FallbackMaterialAsset();
};

class FallbackImportedMeshAsset final : public ImportedMeshAsset {
public:
    FallbackImportedMeshAsset();
};

} // namespace Panda