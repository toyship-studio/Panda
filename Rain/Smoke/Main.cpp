#include "Smoke.hpp"

#include <cstdio>
#include <exception>

namespace {

struct TestCase {
    const char *name;
    RainSmoke::TestFn fn;
};

int runTest(const TestCase &test) {
    std::printf("%s\n", test.name);
    try {
        return test.fn() ? 0 : 1;
    } catch (const std::exception &e) { std::printf("  EXCEPTION: %s\n", e.what()); } catch (...) {
        std::printf("  EXCEPTION: unknown\n");
    }
    return 1;
}

} // namespace

int main() {
    const TestCase tests[] = {
        {"testJsonPrimitiveArray", RainSmoke::testJsonPrimitiveArray},
        {"testJsonNestedRoundTrip", RainSmoke::testJsonNestedRoundTrip},
        {"testJsonOptionalAndCustomCoder", RainSmoke::testJsonOptionalAndCustomCoder},
        {"testJsonEnumAndMapRoundTrip", RainSmoke::testJsonEnumAndMapRoundTrip},
        {"testYamlPrimitiveArray", RainSmoke::testYamlPrimitiveArray},
        {"testYamlNestedRoundTrip", RainSmoke::testYamlNestedRoundTrip},
        {"testYamlOptionalNullDecode", RainSmoke::testYamlOptionalNullDecode},
        {"testCrossFormatJsonToYaml", RainSmoke::testCrossFormatJsonToYaml},
        {"testBinaryRoundTrip", RainSmoke::testBinaryRoundTrip},
        {"testMissingFieldPolicies", RainSmoke::testMissingFieldPolicies},
        {"testFixedArrayFieldMeta", RainSmoke::testFixedArrayFieldMeta},
    };

    int failures = 0;
    for (const TestCase &test : tests) {
        failures += runTest(test);
    }

    if (failures == 0) {
        std::printf("Rain smoke: PASS\n");
        return 0;
    }

    std::printf("Rain smoke: FAIL (%d)\n", failures);
    return 1;
}
