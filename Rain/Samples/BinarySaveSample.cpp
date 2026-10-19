#include "Samples.hpp"

#include "Rain/Coders/BinaryDecoder.hpp"
#include "Rain/Coders/BinaryEncoder.hpp"

#include <cstdint>
#include <ios>
#include <iostream>
#include <sstream>

namespace RainSamples {

bool sampleBinarySave() {
    std::cout << "\n=== Binary save ===\n";

    PlayerSave source = makePlayer();
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    Rain::BinaryEncoder encoder(true);
    encodeTo(encoder, stream, source);

    PlayerSave decoded;
    stream.seekg(0);
    Rain::BinaryDecoder decoder;
    if (!decodeFrom(decoder, stream, decoded)) {
        std::cerr << "Failed to decode binary save\n";
        return false;
    }

    std::cout << "Binary bytes: " << stream.str().size() << "\n";
    std::cout << "Decoded player id: " << static_cast<uint32_t>(decoded.id) << ", name "
              << decoded.name << "\n";
    return true;
}

} // namespace RainSamples
