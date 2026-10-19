#include "Fixtures/Models.hpp"
#include "Smoke.hpp"

#include "Rain/Coders/JsonDecoder.hpp"
#include "Rain/Coders/JsonEncoder.hpp"

#include <sstream>
#include <vector>

namespace RainSmoke {

namespace {

    template<typename T>
    bool jsonRoundTrip(const T &source, T &decoded) {
        std::stringstream encoded;
        Rain::JsonEncoder encoder(true, true);
        encodeTo(encoder, encoded, source);

        std::stringstream input(encoded.str());
        Rain::JsonDecoder decoder;
        return decodeFrom(decoder, input, decoded);
    }

} // namespace

bool testJsonPrimitiveArray() {
    const char *input = R"json(
[
  1.0,
  2.0,
  3.0,
  4.0,
  5.0,
  6.0,
  12.0
]
)json";

    std::stringstream stream(input);
    Rain::JsonDecoder decoder;
    std::vector<float> values;

    bool ok = expect(decodeFrom(decoder, stream, values), "json vector decode");
    ok = expect(values.size() == 7, "json vector size") && ok;
    ok = expect(values.size() > 6 && closeEnough(values[0], 1.0f), "json first vector value") && ok;
    ok = expect(values.size() > 6 && closeEnough(values[6], 12.0f), "json last vector value") && ok;

    std::vector<float> roundTripped;
    ok = expect(jsonRoundTrip(values, roundTripped), "json vector roundtrip decode") && ok;
    ok = expect(roundTripped.size() == values.size(), "json vector roundtrip size") && ok;
    ok = expect(
             roundTripped.size() > 6 && closeEnough(roundTripped[6], values[6]),
             "json vector roundtrip value"
         ) &&
         ok;
    return ok;
}

bool testJsonNestedRoundTrip() {
    Fixtures::NestedPerson source = Fixtures::makeNestedPerson();
    Fixtures::NestedPerson decoded;

    bool ok = expect(jsonRoundTrip(source, decoded), "json nested roundtrip decode");
    ok = expect(source == decoded, "json nested roundtrip equality") && ok;
    return ok;
}

bool testJsonOptionalAndCustomCoder() {
    const char *optionalInput = R"json(
{
  "records": [
    {
      "text": {
        "text": "HELLO!"
      },
      "enabled": true,
      "count": null
    },
    {
      "enabled": false
    }
  ]
}
)json";

    std::stringstream optionalStream(optionalInput);
    Rain::JsonDecoder optionalDecoder;
    Fixtures::OptionalRoot optionalRoot;

    bool ok =
        expect(decodeFrom(optionalDecoder, optionalStream, optionalRoot), "json optional decode");
    bool hasExpectedRecords = expect(optionalRoot.records.size() == 2, "json optional vector size");
    ok = hasExpectedRecords && ok;
    if (!hasExpectedRecords) {
        return false;
    }

    ok = expect(optionalRoot.records[0].text.has_value(), "json optional object value") && ok;
    ok = expect(optionalRoot.records[0].text->text == "HELLO!", "json optional object text") && ok;
    ok = expect(!optionalRoot.records[0].count.has_value(), "json null optional int") && ok;
    ok = expect(!optionalRoot.records[1].text.has_value(), "json missing optional remains empty") &&
         ok;

    Fixtures::CustomEnvelope source = Fixtures::makeCustomEnvelope();
    Fixtures::CustomEnvelope decoded;
    ok = expect(jsonRoundTrip(source, decoded), "json custom coder roundtrip decode") && ok;
    ok = expect(source == decoded, "json custom coder roundtrip equality") && ok;
    ok = expect(decoded.message.has_value(), "json custom optional message") && ok;
    ok = expect(
             decoded.message.has_value() && decoded.message->time == Fixtures::TimeOfDay::Morning,
             "json custom time decoded from string"
         ) &&
         ok;
    return ok;
}

bool testJsonEnumAndMapRoundTrip() {
    Fixtures::MapRoot source = Fixtures::makeMapRoot();
    Fixtures::MapRoot decoded;

    bool ok = expect(jsonRoundTrip(source, decoded), "json unordered_map roundtrip decode");
    ok = expect(source == decoded, "json unordered_map roundtrip equality") && ok;

    Fixtures::NestedPerson person = Fixtures::makeNestedPerson();
    Fixtures::NestedPerson decodedPerson;
    ok = expect(jsonRoundTrip(person, decodedPerson), "json enum roundtrip decode") && ok;
    ok = expect(
             decodedPerson.cars.size() == 2 && decodedPerson.cars[0].size() == 2 &&
                 decodedPerson.cars[0][0].bodyType == Fixtures::BodyType::Sport,
             "json enum roundtrip value"
         ) &&
         ok;
    return ok;
}

} // namespace RainSmoke
