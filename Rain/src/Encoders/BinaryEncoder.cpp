#include "Rain/Coders/BinaryEncoder.hpp"
#include "Rain/TypeRegistry.hpp"

#include <cstring>
#include <limits>

namespace Rain {

namespace {

    constexpr char BinaryMagic[8] = {'R', 'A', 'I', 'N', 'B', 'I', 'N', 0};
    constexpr uint32_t BinaryVersion = 1;

    void writeBytes(std::ostream &stream, const void *data, size_t size) {
        stream.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
    }

    void writeU8(std::ostream &stream, uint8_t value) {
        writeBytes(stream, &value, sizeof(value));
    }

    void writeU32(std::ostream &stream, uint32_t value) {
        uint8_t bytes[4] = {
            static_cast<uint8_t>(value & 0xffu),
            static_cast<uint8_t>((value >> 8u) & 0xffu),
            static_cast<uint8_t>((value >> 16u) & 0xffu),
            static_cast<uint8_t>((value >> 24u) & 0xffu)
        };
        writeBytes(stream, bytes, sizeof(bytes));
    }

    void writeU64(std::ostream &stream, uint64_t value) {
        uint8_t bytes[8] = {
            static_cast<uint8_t>(value & 0xffu),
            static_cast<uint8_t>((value >> 8u) & 0xffu),
            static_cast<uint8_t>((value >> 16u) & 0xffu),
            static_cast<uint8_t>((value >> 24u) & 0xffu),
            static_cast<uint8_t>((value >> 32u) & 0xffu),
            static_cast<uint8_t>((value >> 40u) & 0xffu),
            static_cast<uint8_t>((value >> 48u) & 0xffu),
            static_cast<uint8_t>((value >> 56u) & 0xffu)
        };
        writeBytes(stream, bytes, sizeof(bytes));
    }

    void writeFloat(std::ostream &stream, float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        writeU32(stream, bits);
    }

    void writeDouble(std::ostream &stream, double value) {
        uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        writeU64(stream, bits);
    }

    void writeString(std::ostream &stream, const std::string &value) {
        if (value.size() > std::numeric_limits<uint32_t>::max()) {
            stream.setstate(std::ios::badbit);
            return;
        }
        writeU32(stream, static_cast<uint32_t>(value.size()));
        writeBytes(stream, value.data(), value.size());
    }

    void writeNode(std::ostream &stream, const BinaryNode &node) {
        writeU8(stream, static_cast<uint8_t>(node.type));
        switch (node.type) {
            case BinaryNodeType::Null:
                break;
            case BinaryNodeType::Object: {
                if (node.members.size() > std::numeric_limits<uint32_t>::max()) {
                    stream.setstate(std::ios::badbit);
                    return;
                }
                writeU32(stream, static_cast<uint32_t>(node.members.size()));
                for (const auto &member : node.members) {
                    writeString(stream, member.name);
                    writeNode(stream, *member.value);
                }
                break;
            }
            case BinaryNodeType::Array: {
                if (node.elements.size() > std::numeric_limits<uint32_t>::max()) {
                    stream.setstate(std::ios::badbit);
                    return;
                }
                writeU32(stream, static_cast<uint32_t>(node.elements.size()));
                for (const auto &element : node.elements) {
                    writeNode(stream, *element);
                }
                break;
            }
            case BinaryNodeType::Bool:
                writeU8(stream, node.boolValue ? 1 : 0);
                break;
            case BinaryNodeType::Int:
                writeU32(stream, static_cast<uint32_t>(node.intValue));
                break;
            case BinaryNodeType::UInt:
                writeU32(stream, node.uintValue);
                break;
            case BinaryNodeType::Int64:
                writeU64(stream, static_cast<uint64_t>(node.int64Value));
                break;
            case BinaryNodeType::UInt64:
                writeU64(stream, node.uint64Value);
                break;
            case BinaryNodeType::Float:
                writeFloat(stream, node.floatValue);
                break;
            case BinaryNodeType::Double:
                writeDouble(stream, node.doubleValue);
                break;
            case BinaryNodeType::String:
                writeString(stream, node.stringValue);
                break;
            case BinaryNodeType::UUID:
                writeU32(stream, node.uuidValue);
                break;
        }
    }

} // namespace

BinaryEncoder::BinaryEncoder(bool encodeOptionalAsNull)
    : Encoder(encodeOptionalAsNull)
    , m_root()
    , m_containerStack() {}

void BinaryEncoder::encode(std::ostream &stream, void *data, TypeInfo info) {
    m_root.reset();
    m_containerStack.clear();
    info.encoderFunc(nullptr, this, info, data);

    writeBytes(stream, BinaryMagic, sizeof(BinaryMagic));
    writeU32(stream, BinaryVersion);
    if (m_root) {
        writeNode(stream, *m_root);
    } else {
        BinaryNode nullNode;
        writeNode(stream, nullNode);
    }

    m_root.reset();
    m_containerStack.clear();
}

void BinaryEncoder::encodeNull(const char *key) {
    addNode(key, std::make_unique<BinaryNode>());
}

void BinaryEncoder::beginObject(const char *key) {
    auto node = std::make_unique<BinaryNode>();
    node->type = BinaryNodeType::Object;
    BinaryNode *added = addNode(key, std::move(node));
    m_containerStack.push_back(added);
}

void BinaryEncoder::endObject() {
    if (!m_containerStack.empty()) {
        m_containerStack.pop_back();
    }
}

void BinaryEncoder::beginArray(const char *key) {
    auto node = std::make_unique<BinaryNode>();
    node->type = BinaryNodeType::Array;
    BinaryNode *added = addNode(key, std::move(node));
    m_containerStack.push_back(added);
}

void BinaryEncoder::endArray() {
    if (!m_containerStack.empty()) {
        m_containerStack.pop_back();
    }
}

void BinaryEncoder::encode(const char *key, const bool &data) {
    auto node = std::make_unique<BinaryNode>();
    node->type = BinaryNodeType::Bool;
    node->boolValue = data;
    addNode(key, std::move(node));
}

void BinaryEncoder::encode(const char *key, const int &data) {
    auto node = std::make_unique<BinaryNode>();
    node->type = BinaryNodeType::Int;
    node->intValue = data;
    addNode(key, std::move(node));
}

void BinaryEncoder::encode(const char *key, const uint32_t &data) {
    auto node = std::make_unique<BinaryNode>();
    node->type = BinaryNodeType::UInt;
    node->uintValue = data;
    addNode(key, std::move(node));
}

void BinaryEncoder::encode(const char *key, const int64_t &data) {
    auto node = std::make_unique<BinaryNode>();
    node->type = BinaryNodeType::Int64;
    node->int64Value = data;
    addNode(key, std::move(node));
}

void BinaryEncoder::encode(const char *key, const uint64_t &data) {
    auto node = std::make_unique<BinaryNode>();
    node->type = BinaryNodeType::UInt64;
    node->uint64Value = data;
    addNode(key, std::move(node));
}

void BinaryEncoder::encode(const char *key, const float &data) {
    auto node = std::make_unique<BinaryNode>();
    node->type = BinaryNodeType::Float;
    node->floatValue = data;
    addNode(key, std::move(node));
}

void BinaryEncoder::encode(const char *key, const double &data) {
    auto node = std::make_unique<BinaryNode>();
    node->type = BinaryNodeType::Double;
    node->doubleValue = data;
    addNode(key, std::move(node));
}

void BinaryEncoder::encode(const char *key, const std::string &data) {
    auto node = std::make_unique<BinaryNode>();
    node->type = BinaryNodeType::String;
    node->stringValue = data;
    addNode(key, std::move(node));
}

void BinaryEncoder::encode(const char *key, const char *&data) {
    auto node = std::make_unique<BinaryNode>();
    node->type = BinaryNodeType::String;
    node->stringValue = data ? data : "";
    addNode(key, std::move(node));
}

void BinaryEncoder::encode(const char *key, const UUID &data) {
    auto node = std::make_unique<BinaryNode>();
    node->type = BinaryNodeType::UUID;
    node->uuidValue = data;
    addNode(key, std::move(node));
}

BinaryNode *BinaryEncoder::addNode(const char *key, std::unique_ptr<BinaryNode> node) {
    BinaryNode *raw = node.get();
    if (m_containerStack.empty()) {
        m_root = std::move(node);
        return raw;
    }

    BinaryNode *container = m_containerStack.back();
    if (container->type == BinaryNodeType::Array) {
        container->elements.push_back(std::move(node));
        return raw;
    }

    if (container->type == BinaryNodeType::Object) {
        container->members.push_back({key ? key : "", std::move(node)});
        return raw;
    }

    return raw;
}

void BinaryEncoder::encodeRawJson(const char *key, const std::string &json) {
    (void)key; // opaque values are JSON-only (supportsRawJson): the binary format carries typed data
    (void)json;
}

} // namespace Rain
