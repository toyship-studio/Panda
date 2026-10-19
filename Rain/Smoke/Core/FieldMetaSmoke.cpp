#include "Fixtures/Models.hpp"
#include "Smoke.hpp"

#include "Rain/Coders/JsonDecoder.hpp"
#include "Rain/Coders/JsonEncoder.hpp"

#include <cstddef>
#include <sstream>

namespace RainSmoke {

namespace {

    // C arrays in RAIN_FIELDS: encoded as fixed-length arrays, the field meta carries
    // the element type and the element count (layout generators work from the meta).
    struct ArrayRecord : public Rain::Codable {
        float weights[3] = {0.f, 0.f, 0.f};
        Fixtures::Leaf leaves[2];
        int single = 0;
        const char label[8] = "label"; // a string (const char[N] coder), not an array

        RAIN_FIELDS_BEGIN(ArrayRecord)
        RAIN_FIELD(weights)
        RAIN_FIELD(leaves)
        RAIN_FIELD(single)
        RAIN_FIELD(label)
        RAIN_FIELDS_END
    };

} // namespace

bool testFixedArrayFieldMeta() {
    const std::vector<Rain::FieldInfo> fields = ArrayRecord::getFields();
    bool ok = expect(fields.size() == 4, "array record field count");
    if (!ok) { return false; }
    const Rain::TypeId floatId = Rain::getTypeRegistry()->findOrCreateType<float>().id;
    const Rain::TypeId leafId = Rain::getTypeRegistry()->findOrCreateType<Fixtures::Leaf>().id;
    ok = expect(fields[0].isArray() && fields[0].count == 3, "float[3] meta count") && ok;
    ok = expect(fields[0].elementTypeId == floatId, "float[3] meta element type") && ok;
    ok = expect(fields[0].typeId != floatId, "float[3] typeId is the array type") && ok;
    ok = expect(fields[1].isArray() && fields[1].count == 2, "Leaf[2] meta count") && ok;
    ok = expect(fields[1].elementTypeId == leafId, "Leaf[2] meta element type") && ok;
    ok = expect(!fields[2].isArray() && fields[2].elementTypeId == fields[2].typeId, "scalar meta") && ok;
    ok = expect(!fields[3].isArray(), "char[N] is a string, not an array") && ok;
    ok = expect(fields[1].offset == offsetof(ArrayRecord, leaves), "Leaf[2] offset") && ok;

    ArrayRecord source;
    source.weights[0] = 1.5f;
    source.weights[1] = -2.f;
    source.weights[2] = 4.25f;
    source.leaves[0] = Fixtures::Leaf{"first"};
    source.leaves[1] = Fixtures::Leaf{"second"};
    source.single = 7;

    std::stringstream encoded;
    Rain::JsonEncoder encoder(true, true);
    encodeTo(encoder, encoded, source);

    ArrayRecord decoded;
    std::stringstream input(encoded.str());
    Rain::JsonDecoder decoder;
    ok = expect(decodeFrom(decoder, input, decoded), "fixed array json decode") && ok;
    ok = expect(closeEnough(decoded.weights[0], 1.5f), "fixed array element 0") && ok;
    ok = expect(closeEnough(decoded.weights[2], 4.25f), "fixed array element 2") && ok;
    ok = expect(decoded.leaves[0] == source.leaves[0] && decoded.leaves[1] == source.leaves[1], "fixed array of objects") && ok;
    ok = expect(decoded.single == 7, "scalar next to arrays") && ok;
    ok = expect(std::string(decoded.label) == "label", "char array as string") && ok;

    // Extra elements of the stream are dropped, missing ones stay as they were.
    const char *longer = R"json({"weights": [9.0, 8.0, 7.0, 6.0], "leaves": [{"value": "only"}], "single": 1, "label": "x"})json";
    ArrayRecord partial = source;
    std::stringstream longerStream(longer);
    ok = expect(decodeFrom(decoder, longerStream, partial), "fixed array decode with other length") && ok;
    ok = expect(closeEnough(partial.weights[2], 7.f), "extra stream elements dropped") && ok;
    ok = expect(partial.leaves[0].value == "only" && partial.leaves[1].value == "second", "missing elements kept") && ok;
    return ok;
}

} // namespace RainSmoke
