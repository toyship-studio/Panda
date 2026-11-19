#include "Panda/Assets/Base/AssetRefImpl.hpp"
#include "Panda/Assets/FontAsset.hpp"
#include "Panda/Assets/MaterialAsset.hpp"
#include "Panda/Assets/PostprocessingAsset.hpp"
#include "Panda/Assets/RuntimeMeshAsset.hpp"
#include "Panda/Assets/ImportedMeshAsset.hpp"
#include "Panda/Assets/TextureAsset.hpp"
#include "Panda/Assets/ShaderAsset.hpp"
#include "Panda/Prefabs/PrefabAsset.hpp"

namespace Panda {

template class AssetRef<Asset>;
template class AssetRef<ShaderAsset>;
template class AssetRef<MaterialAsset>;
template class AssetRef<TextureAsset>;
template class AssetRef<RuntimeMeshAsset>;
template class AssetRef<ImportedMeshAsset>;
template class AssetRef<PostprocessingAsset>;
template class AssetRef<FontAsset>;
template class AssetRef<PrefabAsset>;

} // namespace Panda
