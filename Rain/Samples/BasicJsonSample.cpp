#include "Samples.hpp"

#include "Rain/Coders/JsonDecoder.hpp"
#include "Rain/Coders/JsonEncoder.hpp"

#include <iostream>
#include <sstream>

namespace RainSamples {

bool sampleBasicJson() {
    std::cout << "\n=== Basic JSON save ===\n";

    PlayerSave source = makePlayer();
    std::stringstream stream;
    Rain::JsonEncoder encoder(true, true);
    encodeTo(encoder, stream, source);

    std::cout << stream.str() << "\n";

    PlayerSave decoded;
    std::stringstream input(stream.str());
    Rain::JsonDecoder decoder;
    if (!decodeFrom(decoder, input, decoded)) {
        std::cerr << "Failed to decode JSON save\n";
        return false;
    }

    std::cout << "Decoded player: " << decoded.name << ", level " << decoded.level << ", items "
              << decoded.inventory.size() << "\n";
    return true;
}

} // namespace RainSamples
