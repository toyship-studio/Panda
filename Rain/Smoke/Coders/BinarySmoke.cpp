#include "Fixtures/Models.hpp"
#include "Smoke.hpp"

#include "Rain/Coders/BinaryDecoder.hpp"
#include "Rain/Coders/BinaryEncoder.hpp"

#include <ios>
#include <sstream>

namespace RainSmoke {

bool testBinaryRoundTrip() {
    Fixtures::Player source = Fixtures::makePlayer();
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);

    Rain::BinaryEncoder encoder(true);
    encodeTo(encoder, stream, source);

    Fixtures::Player decoded;
    Rain::BinaryDecoder decoder;
    stream.seekg(0);

    bool ok = expect(decodeFrom(decoder, stream, decoded), "binary roundtrip decode");
    ok = expect(source == decoded, "binary roundtrip equality") && ok;
    ok = expect(!stream.str().empty(), "binary output is not empty") && ok;
    return ok;
}

} // namespace RainSmoke
