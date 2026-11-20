#include "Fixtures/Models.hpp"
#include "Smoke.hpp"

#include "Rain/Coders/JsonDecoder.hpp"
#include "Rain/Coders/JsonEncoder.hpp"
#include "Rain/Value.hpp"

#include <sstream>

namespace RainSmoke {

// A value is built, edited and written out the way the JSON text has it.
bool testValueEditing() {
    Rain::Value value = Rain::Value::object();
    value.set("name", "lamp");
    value.set("count", 3);
    Rain::Value &list = value.set("list", Rain::Value::array());
    list.push(true);
    list.push(nullptr);
    list.push(-7);
    value.set("ratio", 0.5);
    value.set("count", 4); // a key it has takes the value in its place

    bool ok = expect(Rain::writeJson(value) == R"({"name":"lamp","count":4,"list":[true,null,-7],"ratio":0.5})", "value text");
    ok = expect(value.find("list") != nullptr && value.find("list")->size() == 3, "value find") && ok;
    ok = expect(value.erase("name") && !value.erase("name") && value.find("name") == nullptr, "value erase") && ok;
    ok = expect(value.find("count")->isInt() && value.find("count")->getInt() == 4, "value integer") && ok;
    ok = expect(value.find("list")->items()[2].isInt() && !value.find("list")->items()[2].isUint(), "value negative") && ok;

    Rain::Value parsed;
    ok = expect(Rain::parseJson(Rain::writeJson(value, Rain::JsonLayout::Pretty), parsed) && parsed == value, "value text round trip") && ok;
    ok = expect(!Rain::parseJson("{\"a\":", parsed) && parsed == value, "value bad text leaves it") && ok;
    return ok;
}

// Equal values: numbers by value (an integer equals the same floating-point number), objects
// whatever the order of their members, arrays item by item.
bool testValueEquality() {
    Rain::Value a;
    Rain::Value b;
    bool ok = expect(Rain::parseJson(R"({"x":1,"y":[1,2],"z":{"k":"v"}})", a), "parse a");
    ok = expect(Rain::parseJson(R"({"z":{"k":"v"},"y":[1,2],"x":1.0})", b), "parse b") && ok;
    ok = expect(a == b, "objects in another order, 1 and 1.0") && ok;
    ok = expect(Rain::parseJson(R"({"x":1,"y":[2,1],"z":{"k":"v"}})", b) && a != b, "arrays keep their order") && ok;
    ok = expect(Rain::Value(-1) != Rain::Value(uint64_t(18446744073709551615ull)), "a negative integer is not a large one") && ok;
    ok = expect(Rain::Value(uint32_t(5)) == Rain::Value(int64_t(5)), "one integer whatever its type") && ok;
    ok = expect(Rain::Value("1") != Rain::Value(1), "a string is not a number") && ok;
    return ok;
}

// Typed data → a value → typed data, and the value is the one its text is written from.
bool testJsonValueRoundTrip() {
    Fixtures::NestedPerson person;
    person.name = "Ada";
    person.age = 36;
    person.hungry = true;
    person.scores = {3, -2, 7};
    person.surname = "Lovelace";

    Rain::JsonEncoder encoder;
    const Rain::Value value = encoder.encodeValue(person);
    std::stringstream text;
    Rain::JsonEncoder textEncoder;
    encodeTo(textEncoder, text, person);
    Rain::Value parsed;
    bool ok = expect(Rain::parseJson(text.str(), parsed) && parsed == value, "the value is what the text says");
    ok = expect(value.find("scores") != nullptr && value.find("scores")->size() == 3, "the value holds the list") && ok;

    Fixtures::NestedPerson decoded;
    Rain::JsonDecoder decoder;
    ok = expect(decoder.decodeValue(value, decoded), "decode from the value") && ok;
    ok = expect(decoded.name == "Ada" && decoded.age == 36 && decoded.hungry && decoded.surname == "Lovelace", "decoded fields") && ok;
    ok = expect(decoded.scores == std::vector<int>({3, -2, 7}), "decoded list") && ok;
    return ok;
}

} // namespace RainSmoke
