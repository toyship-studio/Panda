#include "Fixtures/Models.hpp"
#include "Smoke.hpp"

#include "Rain/Coders/JsonDecoder.hpp"
#include "Rain/Coders/YamlDecoder.hpp"
#include "Rain/Coders/YamlEncoder.hpp"

#include <sstream>
#include <vector>

namespace RainSmoke {

namespace {

    template<typename T>
    bool yamlRoundTrip(const T &source, T &decoded) {
        std::stringstream encoded;
        Rain::YamlEncoder encoder(true);
        encodeTo(encoder, encoded, source);

        std::stringstream input(encoded.str());
        Rain::YamlDecoder decoder;
        return decodeFrom(decoder, input, decoded);
    }

} // namespace

bool testYamlPrimitiveArray() {
    const char *input = R"yaml(
---
- 1.1
- 2.0
- 3.3
- 4.0
- 5.5
- 6.0
...
)yaml";

    std::stringstream stream(input);
    Rain::YamlDecoder decoder;
    std::vector<float> values;

    bool ok = expect(decodeFrom(decoder, stream, values), "yaml vector decode");
    ok = expect(values.size() == 6, "yaml vector size") && ok;
    ok = expect(values.size() > 4 && closeEnough(values[0], 1.1f), "yaml first vector value") && ok;
    ok =
        expect(values.size() > 4 && closeEnough(values[4], 5.5f), "yaml middle vector value") && ok;
    return ok;
}

bool testYamlNestedRoundTrip() {
    Fixtures::NestedPerson source = Fixtures::makeNestedPerson();
    Fixtures::NestedPerson decoded;

    bool ok = expect(yamlRoundTrip(source, decoded), "yaml nested roundtrip decode");
    ok = expect(source == decoded, "yaml nested roundtrip equality") && ok;
    return ok;
}

bool testYamlOptionalNullDecode() {
    const char *input = R"yaml(
---
records:
  - text:
      text: HELLO!
    enabled: true
    count: ~
...
)yaml";

    std::stringstream stream(input);
    Rain::YamlDecoder decoder;
    Fixtures::OptionalRoot root;

    bool ok = expect(decodeFrom(decoder, stream, root), "yaml optional decode");
    bool hasExpectedRecords = expect(root.records.size() == 1, "yaml optional vector size");
    ok = hasExpectedRecords && ok;
    if (!hasExpectedRecords) {
        return false;
    }

    ok = expect(root.records[0].text.has_value(), "yaml optional object value") && ok;
    ok = expect(root.records[0].text->text == "HELLO!", "yaml optional object text") && ok;
    ok = expect(root.records[0].enabled, "yaml bool value") && ok;
    ok = expect(!root.records[0].count.has_value(), "yaml null optional int") && ok;
    return ok;
}

bool testCrossFormatJsonToYaml() {
    const char *input = R"json(
{
  "array": [
    {
      "text": "B TEXT",
      "objects": {
        "key1": {
          "text": "test1",
          "number": -1
        },
        "key2": {
          "text": "test2",
          "number": 1
        },
        "key3": {
          "text": "test3",
          "number": 10
        }
      },
      "value": 3.14
    }
  ],
  "message": "TEST MESSAGE"
}
)json";

    std::stringstream jsonStream(input);
    Rain::JsonDecoder jsonDecoder;
    Fixtures::MapRoot jsonDecoded;

    bool ok = expect(decodeFrom(jsonDecoder, jsonStream, jsonDecoded), "cross json decode");

    std::stringstream yamlStream;
    Rain::YamlEncoder yamlEncoder;
    encodeTo(yamlEncoder, yamlStream, jsonDecoded);

    std::stringstream yamlInput(yamlStream.str());
    Rain::YamlDecoder yamlDecoder;
    Fixtures::MapRoot yamlDecoded;

    ok = expect(decodeFrom(yamlDecoder, yamlInput, yamlDecoded), "cross yaml decode") && ok;
    ok = expect(jsonDecoded == yamlDecoded, "cross json to yaml equality") && ok;
    return ok;
}

} // namespace RainSmoke
