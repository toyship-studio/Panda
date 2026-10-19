#pragma once

#include "Panda/Assets/Base/Asset.hpp"
#include "Panda/Assets/Base/Path.hpp"

#include <utility>

namespace Panda {

class WorldAsset final : public Asset {
public:
    explicit WorldAsset(path_t worldPath = {})
        : Asset(AssetType::WORLD)
        , m_worldPath(std::move(worldPath)) {}

    const path_t &getWorldPath() const {
        return m_worldPath;
    }

private:
    path_t m_worldPath;
};

} // namespace Panda
