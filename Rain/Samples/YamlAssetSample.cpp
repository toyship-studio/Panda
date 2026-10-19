#include "Samples.hpp"

#include "Rain/Codable.hpp"
#include "Rain/Coders/YamlDecoder.hpp"
#include "Rain/Coders/YamlEncoder.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace RainSamples::YamlAssetSample {

struct SpawnRule : public Rain::Codable {
    std::string entity;
    float density = 0.0f;

    RAIN_FIELDS_BEGIN(SpawnRule)
    RAIN_FIELD(entity)
    RAIN_FIELD(density)
    RAIN_FIELDS_END
};

struct BiomeAsset : public Rain::Codable {
    std::string name;
    std::filesystem::path terrainMaterial;
    std::vector<SpawnRule> spawns;

    RAIN_FIELDS_BEGIN(BiomeAsset)
    RAIN_FIELD(name)
    RAIN_FIELD(terrainMaterial)
    RAIN_FIELD(spawns)
    RAIN_FIELDS_END
};

} // namespace RainSamples::YamlAssetSample

namespace RainSamples {

bool sampleYamlAsset() {
    std::cout << "\n=== YAML asset ===\n";

    const char *assetText = R"yaml(
---
name: Neverwood
terrainMaterial: Assets/Materials/Neverwood.mat
spawns:
  - entity: Firefly
    density: 0.5
  - entity: MossGolem
    density: 0.125
...
)yaml";

    std::stringstream input(assetText);
    Rain::YamlDecoder decoder;
    YamlAssetSample::BiomeAsset asset;
    if (!decodeFrom(decoder, input, asset)) {
        std::cerr << "Failed to decode YAML asset\n";
        return false;
    }

    std::stringstream output;
    Rain::YamlEncoder encoder;
    encodeTo(encoder, output, asset);

    std::cout << output.str() << "\n";
    std::cout << "Loaded biome: " << asset.name << ", material "
              << asset.terrainMaterial.generic_string() << "\n";
    return true;
}

} // namespace RainSamples
