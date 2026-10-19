#include "Rain/Coders/BinaryDecoder.hpp"
#include "Rain/TypeRegistry.hpp"

#include <cstring>

namespace Rain {

namespace {

    constexpr char BinaryMagic[8] = {'R', 'A', 'I', 'N', 'B', 'I', 'N', 0};
    constexpr uint32_t BinaryVersion = 1;

    bool readBytes(std::istream &stream, void *data, size_t size) {
        stream.read(static_cast<char *>(data), static_cast<std::streamsize>(size));
        return stream.good();
    }

    bool readU8(std::istream &stream, uint8_t &value) {
        return readBytes(stream, &value, sizeof(value));
    }

    bool readU32(std::istream &stream, uint32_t &value) {
        uint8_t bytes[4];
        if (!readBytes(stream, bytes, sizeof(bytes))) {
            return false;
        }
        value = static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8u) |
                (static_cast<uint32_t>(bytes[2]) << 16u) | (static_cast<uint32_t>(bytes[3]) << 24u);
        return true;
    }

    bool readU64(std::istream &stream, uint64_t &value) {
        uint8_t bytes[8];
        if (!readBytes(stream, bytes, sizeof(bytes))) {
            return false;
        }
        value =
            static_cast<uint64_t>(bytes[0]) | (static_cast<uint64_t>(bytes[1]) << 8u) |
            (static_cast<uint64_t>(bytes[2]) << 16u) | (static_cast<uint64_t>(bytes[3]) << 24u) |
            (static_cast<uint64_t>(bytes[4]) << 32u) | (static_cast<uint64_t>(bytes[5]) << 40u) |
            (static_cast<uint64_t>(bytes[6]) << 48u) | (static_cast<uint64_t>(bytes[7]) << 56u);
        return true;
    }

    bool readFloat(std::istream &stream, float &value) {
        uint32_t bits;
        if (!readU32(stream, bits)) {
            return false;
        }
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }

    bool readDouble(std::istream &stream, double &value) {
        uint64_t bits;
        if (!readU64(stream, bits)) {
            return false;
        }
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }

    bool readString(std::istream &stream, std::string &value) {
        uint32_t size;
        if (!readU32(stream, size)) {
            return false;
        }
        value.resize(size);
        if (size == 0) {
            return true;
        }
        return readBytes(stream, value.data(), size);
    }

    std::unique_ptr<BinaryNode> readNode(std::istream &stream) {
        uint8_t tag;
        if (!readU8(stream, tag)) {
            return nullptr;
        }
        auto node = std::make_unique<BinaryNode>();
        node->type = static_cast<BinaryNodeType>(tag);
        switch (node->type) {
            case BinaryNodeType::Null:
                return node;
            case BinaryNodeType::Object: {
                uint32_t count;
                if (!readU32(stream, count)) {
                    return nullptr;
                }
                node->members.reserve(count);
                for (uint32_t i = 0; i < count; ++i) {
                    std::string name;
                    if (!readString(stream, name)) {
                        return nullptr;
                    }
                    std::unique_ptr<BinaryNode> value = readNode(stream);
                    if (!value) {
                        return nullptr;
                    }
                    node->members.push_back({std::move(name), std::move(value)});
                }
                return node;
            }
            case BinaryNodeType::Array: {
                uint32_t count;
                if (!readU32(stream, count)) {
                    return nullptr;
                }
                node->elements.reserve(count);
                for (uint32_t i = 0; i < count; ++i) {
                    std::unique_ptr<BinaryNode> value = readNode(stream);
                    if (!value) {
                        return nullptr;
                    }
                    node->elements.push_back(std::move(value));
                }
                return node;
            }
            case BinaryNodeType::Bool: {
                uint8_t value;
                if (!readU8(stream, value)) {
                    return nullptr;
                }
                node->boolValue = value != 0;
                return node;
            }
            case BinaryNodeType::Int: {
                uint32_t value;
                if (!readU32(stream, value)) {
                    return nullptr;
                }
                node->intValue = static_cast<int>(value);
                return node;
            }
            case BinaryNodeType::UInt:
                return readU32(stream, node->uintValue) ? std::move(node) : nullptr;
            case BinaryNodeType::Int64: {
                uint64_t value;
                if (!readU64(stream, value)) {
                    return nullptr;
                }
                node->int64Value = static_cast<int64_t>(value);
                return node;
            }
            case BinaryNodeType::UInt64:
                return readU64(stream, node->uint64Value) ? std::move(node) : nullptr;
            case BinaryNodeType::Float:
                return readFloat(stream, node->floatValue) ? std::move(node) : nullptr;
            case BinaryNodeType::Double:
                return readDouble(stream, node->doubleValue) ? std::move(node) : nullptr;
            case BinaryNodeType::String:
                return readString(stream, node->stringValue) ? std::move(node) : nullptr;
            case BinaryNodeType::UUID: {
                uint32_t value;
                if (!readU32(stream, value)) {
                    return nullptr;
                }
                node->uuidValue = UUID(value);
                return node;
            }
        }
        return nullptr;
    }

} // namespace

BinaryDecoder::BinaryDecoder(DecoderSettings settings)
    : Decoder(settings)
    , m_root()
    , m_isArray(false)
    , m_objectStack()
    , m_arrayStack() {}

bool BinaryDecoder::decode(std::istream &stream, void *data, TypeInfo info) {
    m_root.reset();
    m_objectStack.clear();
    m_arrayStack.clear();
    m_isArray = false;

    char magic[8];
    if (!readBytes(stream, magic, sizeof(magic))) {
        return false;
    }
    if (std::memcmp(magic, BinaryMagic, sizeof(BinaryMagic)) != 0) {
        return false;
    }

    uint32_t version;
    if (!readU32(stream, version) || version != BinaryVersion) {
        return false;
    }

    m_root = readNode(stream);
    if (!m_root) {
        return false;
    }

    bool result = info.decoderFunc(nullptr, this, info, data);
    m_root.reset();
    m_objectStack.clear();
    m_arrayStack.clear();
    m_isArray = false;
    return result;
}

bool BinaryDecoder::hasMember(const char *key) {
    if (!key || m_isArray) {
        return false;
    }
    return findMember(currentObject(), key) != nullptr;
}

std::vector<std::string> BinaryDecoder::getMemberKeys() {
    std::vector<std::string> result;
    BinaryNode *object = currentObject();
    if (!object || object->type != BinaryNodeType::Object) {
        return result;
    }
    result.reserve(object->members.size());
    for (const auto &member : object->members) {
        result.push_back(member.name);
    }
    return result;
}

bool BinaryDecoder::beginObject(const char *key) {
    BinaryNode *node = nodeForKey(key);
    if (!node || node->type != BinaryNodeType::Object) {
        return false;
    }
    m_objectStack.push_back({node, m_isArray});
    m_isArray = false;
    return true;
}

void BinaryDecoder::endObject() {
    if (m_objectStack.empty()) {
        return;
    }
    m_isArray = m_objectStack.back().isPartOfArray;
    m_objectStack.pop_back();
}

bool BinaryDecoder::beginArray(const char *key) {
    BinaryNode *node = nodeForKey(key);
    if (!node || node->type != BinaryNodeType::Array) {
        return false;
    }
    m_arrayStack.push_back({node, 0, m_isArray});
    m_isArray = true;
    return true;
}

bool BinaryDecoder::arrayHasElement() {
    return !m_arrayStack.empty() &&
           m_arrayStack.back().index < m_arrayStack.back().ref->elements.size();
}

void BinaryDecoder::arrayNext() {
    if (!m_arrayStack.empty()) {
        ++m_arrayStack.back().index;
    }
}

void BinaryDecoder::endArray() {
    if (m_arrayStack.empty()) {
        return;
    }
    m_isArray = m_arrayStack.back().isPartOfArray;
    m_arrayStack.pop_back();
}

bool BinaryDecoder::decode(const char *key, bool &data) {
    BinaryNode *node = nodeForKey(key);
    if (!node || node->type != BinaryNodeType::Bool) {
        return false;
    }
    data = node->boolValue;
    return true;
}

bool BinaryDecoder::decode(const char *key, int &data) {
    BinaryNode *node = nodeForKey(key);
    if (!node || node->type != BinaryNodeType::Int) {
        return false;
    }
    data = node->intValue;
    return true;
}

bool BinaryDecoder::decode(const char *key, uint32_t &data) {
    BinaryNode *node = nodeForKey(key);
    if (!node || node->type != BinaryNodeType::UInt) {
        return false;
    }
    data = node->uintValue;
    return true;
}

bool BinaryDecoder::decode(const char *key, int64_t &data) {
    BinaryNode *node = nodeForKey(key);
    if (!node || node->type != BinaryNodeType::Int64) {
        return false;
    }
    data = node->int64Value;
    return true;
}

bool BinaryDecoder::decode(const char *key, uint64_t &data) {
    BinaryNode *node = nodeForKey(key);
    if (!node || node->type != BinaryNodeType::UInt64) {
        return false;
    }
    data = node->uint64Value;
    return true;
}

bool BinaryDecoder::decode(const char *key, float &data) {
    BinaryNode *node = nodeForKey(key);
    if (!node || node->type != BinaryNodeType::Float) {
        return false;
    }
    data = node->floatValue;
    return true;
}

bool BinaryDecoder::decode(const char *key, double &data) {
    BinaryNode *node = nodeForKey(key);
    if (!node || node->type != BinaryNodeType::Double) {
        return false;
    }
    data = node->doubleValue;
    return true;
}

bool BinaryDecoder::decode(const char *key, std::string &data) {
    BinaryNode *node = nodeForKey(key);
    if (!node || node->type != BinaryNodeType::String) {
        return false;
    }
    data = node->stringValue;
    return true;
}

bool BinaryDecoder::decode(const char *key, UUID &data) {
    BinaryNode *node = nodeForKey(key);
    if (!node || node->type != BinaryNodeType::UUID) {
        return false;
    }
    data = node->uuidValue;
    return true;
}

BinaryNode *BinaryDecoder::currentObject() {
    return m_objectStack.empty() ? m_root.get() : m_objectStack.back().ref;
}

BinaryNode *BinaryDecoder::currentArrayElement() {
    if (!arrayHasElement()) {
        return nullptr;
    }
    return m_arrayStack.back().ref->elements[m_arrayStack.back().index].get();
}

BinaryNode *BinaryDecoder::findMember(BinaryNode *object, const char *key) {
    if (!object || object->type != BinaryNodeType::Object || !key) {
        return nullptr;
    }
    for (auto &member : object->members) {
        if (member.name == key) {
            return member.value.get();
        }
    }
    return nullptr;
}

BinaryNode *BinaryDecoder::nodeForKey(const char *key) {
    if (m_isArray) {
        return currentArrayElement();
    }
    if (m_objectStack.empty() && key == nullptr) {
        return m_root.get();
    }
    return findMember(currentObject(), key);
}

bool BinaryDecoder::decodeRawJson(const char *key, std::string &json) {
    (void)key; // opaque values are JSON-only (supportsRawJson)
    (void)json;
    return false;
}

} // namespace Rain
