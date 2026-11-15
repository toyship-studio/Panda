#pragma once

#include "Panda/Assets/Base/Asset.hpp"

namespace Panda {

// A world as an asset: the handle a reference to a world file resolves to (a script field, the
// start world of the project). The world itself is read from the file by whoever opens it.
class WorldAsset final : public Asset {
public:
    WorldAsset()
        : Asset(AssetType::WORLD) {}
};

} // namespace Panda
