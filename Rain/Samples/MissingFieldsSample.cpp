#include "Samples.hpp"

#include "Rain/Coder.hpp"
#include "Rain/Coders/JsonDecoder.hpp"

#include <iostream>
#include <sstream>

namespace RainSamples {

bool sampleMissingFields() {
    std::cout << "\n=== Missing fields compatibility ===\n";

    const char *oldSave = R"json(
{
  "id": 1001,
  "name": "Mira",
  "level": 7,
  "alive": true,
  "inventory": [
    {
      "id": "torch",
      "count": 1
    }
  ]
}
)json";

    PlayerSave decoded;
    decoded.title = "Default title from current game version";
    decoded.position = Vec3(0.0f, 1.0f, 0.0f);

    std::stringstream input(oldSave);
    Rain::JsonDecoder decoder({Rain::MissingFieldPolicy::KeepCurrentValue});
    if (!decodeFrom(decoder, input, decoded)) {
        std::cerr << "Failed to decode old save\n";
        return false;
    }

    std::cout << "Old save decoded: " << decoded.name << ", level " << decoded.level << "\n";
    std::cout << "Missing title preserved as: " << decoded.title.value_or("<none>") << "\n";
    std::cout << "Missing position preserved as: " << decoded.position.x << ", "
              << decoded.position.y << ", " << decoded.position.z << "\n";
    return true;
}

} // namespace RainSamples
