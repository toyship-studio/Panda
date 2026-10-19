#pragma once

#include "Rain/UUID.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Rain {

enum class BinaryNodeType : uint8_t {
    Null,
    Object,
    Array,
    Bool,
    Int,
    UInt,
    Int64,
    UInt64,
    Float,
    Double,
    String,
    UUID,
};

struct BinaryNode;

struct BinaryObjectMember final {
    std::string name;
    std::unique_ptr<BinaryNode> value;
};

struct BinaryNode final {
    BinaryNodeType type = BinaryNodeType::Null;
    bool boolValue = false;
    int intValue = 0;
    uint32_t uintValue = 0;
    int64_t int64Value = 0;
    uint64_t uint64Value = 0;
    float floatValue = 0.f;
    double doubleValue = 0.0;
    std::string stringValue;
    Rain::UUID uuidValue = Rain::UUID(0);
    std::vector<BinaryObjectMember> members;
    std::vector<std::unique_ptr<BinaryNode>> elements;
};

} // namespace Rain
