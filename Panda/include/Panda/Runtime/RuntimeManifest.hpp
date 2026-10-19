#pragma once

#include <Rain/Rain.hpp>
#include <string>

namespace Panda {

struct RuntimeManifest final : public Rain::Codable {
    std::string worldPath;
    std::string gameName; // save directory in the OS path (persistent data path)

    RAIN_FIELDS_BEGIN(RuntimeManifest)
    RAIN_FIELD(worldPath)
    RAIN_FIELD(gameName)
    RAIN_FIELDS_END

    void clear() {
        worldPath.clear();
        gameName.clear();
    }
};

} // namespace Panda
