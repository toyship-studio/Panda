#pragma once

#include <Rain/Rain.hpp>
#include <string>
#include <vector>

namespace Panda {

struct RuntimeManifest final : public Rain::Codable {
    std::string worldPath;
    std::string gameName; // save directory in the OS path (persistent data path)
    // The shader features of the project (ShaderFeatures.hpp): the shaders of the game are cooked
    // for this set, the runtime enables exactly it.
    std::vector<std::string> shaderFeatures;
    // The settings of the project the game reads — what a frame may cost (the shadow maps, the
    // ambient occlusion, the distances of the terrain) — as one object in the binary format of
    // Rain (SettingsMapper::encodeGameSettings); the runtime puts them into
    // GameContext::getProjectSettings.
    std::string projectSettings;

    RAIN_FIELDS_BEGIN(RuntimeManifest)
    RAIN_FIELD(worldPath)
    RAIN_FIELD(gameName)
    RAIN_FIELD(shaderFeatures)
    RAIN_FIELD(projectSettings)
    RAIN_FIELDS_END

    void clear() {
        worldPath.clear();
        gameName.clear();
        shaderFeatures.clear();
        projectSettings.clear();
    }
};

} // namespace Panda
