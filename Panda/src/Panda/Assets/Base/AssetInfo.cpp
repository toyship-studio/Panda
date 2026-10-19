#include "Panda/Assets/Base/AssetInfo.hpp"
#include "Panda/Extensions/Extensions.hpp"
#include "Panda/GameLogic/GameContext.hpp"

namespace Panda {

const char *AssetInfo::getTypeStr() const {
    const AssetKind *kind = GameContext::getExtensions().assetKinds.find(type);
    return kind != nullptr ? kind->displayName.c_str() : "Unknown";
}

} // namespace Panda
