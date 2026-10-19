#pragma once

#include "Rain/Coder.hpp"

#include <cmath>
#include <cstdio>
#include <istream>
#include <ostream>
#include <string_view>

namespace RainSmoke {

inline bool expect(bool condition, const char *what) {
    if (!condition) {
        std::printf("  FAIL: %s\n", what);
    }
    return condition;
}

inline bool closeEnough(float left, float right) {
    return std::abs(left - right) < 0.0001f;
}

inline bool closeEnough(double left, double right) {
    return std::abs(left - right) < 0.0001;
}

using TestFn = bool (*)();

template<typename T>
void encodeTo(Rain::Encoder &encoder, std::ostream &stream, const T &data) {
    encoder.encode(stream, data);
}

template<typename T>
bool decodeFrom(Rain::Decoder &decoder, std::istream &stream, T &data) {
    return decoder.decode(stream, data);
}

bool testJsonPrimitiveArray();
bool testJsonNestedRoundTrip();
bool testJsonOptionalAndCustomCoder();
bool testJsonEnumAndMapRoundTrip();

bool testYamlPrimitiveArray();
bool testYamlNestedRoundTrip();
bool testYamlOptionalNullDecode();
bool testCrossFormatJsonToYaml();

bool testBinaryRoundTrip();

bool testMissingFieldPolicies();

bool testFixedArrayFieldMeta();

} // namespace RainSmoke
