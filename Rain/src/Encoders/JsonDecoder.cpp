#include "Rain/Coders/JsonDecoder.hpp"
#include "Rain/TypeRegistry.hpp"

#include <istream>

namespace Rain {

JsonDecoder::JsonDecoder(DecoderSettings settings)
    : Decoder(settings)
    , m_isArray(false)
    , m_root(nullptr)
    , m_objectStack()
    , m_arrayStack() {}

bool JsonDecoder::decodeValue(const Value &value, void *data, TypeInfo info) {
    m_root = &value;
    const bool result = info.decoderFunc(nullptr, this, info, data);
    m_objectStack.clear();
    m_arrayStack.clear();
    m_isArray = false;
    m_root = nullptr;
    return result;
}

bool JsonDecoder::decode(std::istream &istream, void *data, TypeInfo info) {
    Value value;
    if (!parseJson(istream, value)) { return false; }
    return decodeValue(value, data, info);
}

const Value &JsonDecoder::currentObject() const {
    return m_objectStack.empty() ? *m_root : *m_objectStack.back().value;
}

const Value *JsonDecoder::current(const char *key) const {
    if (m_isArray) {
        const Array &array = m_arrayStack.back();
        return array.index < array.value->size() ? &array.value->items()[array.index] : nullptr;
    }
    return key != nullptr ? currentObject().find(key) : nullptr;
}

bool JsonDecoder::hasMember(const char *key) {
    if (!key || m_isArray) { return false; }
    return currentObject().find(key) != nullptr;
}

std::vector<std::string> JsonDecoder::getMemberKeys() {
    std::vector<std::string> result;
    for (const Value::Member &member : currentObject().members()) { result.push_back(member.key); }
    return result;
}

bool JsonDecoder::beginObject(const char *key) {
    const Value *object;
    if (m_isArray) {
        object = current(key); // an item of an array is read as what it is
        if (object == nullptr) { return false; }
    } else if (m_objectStack.empty()) {
        object = m_root;
    } else {
        object = current(key);
        if (object == nullptr || !object->isObject()) { return false; }
    }
    m_objectStack.push_back({object, m_isArray});
    m_isArray = false;
    return true;
}

void JsonDecoder::endObject() {
    m_isArray = m_objectStack.back().isPartOfArray;
    m_objectStack.pop_back();
}

bool JsonDecoder::beginArray(const char *key) {
    const Value *array = m_isArray || !m_objectStack.empty() ? current(key) : m_root;
    if (array == nullptr || !array->isArray()) { return false; }
    m_arrayStack.push_back({array, 0, m_isArray});
    m_isArray = true;
    return true;
}

bool JsonDecoder::arrayHasElement() {
    return !m_arrayStack.empty() && m_arrayStack.back().index < m_arrayStack.back().value->size();
}

void JsonDecoder::arrayNext() {
    ++m_arrayStack.back().index;
}

void JsonDecoder::endArray() {
    m_isArray = m_arrayStack.back().isPartOfArray;
    m_arrayStack.pop_back();
}

bool JsonDecoder::decode(const char *key, int &data) {
    const Value *value = current(key);
    if (value == nullptr || !value->isInt()) { return false; }
    data = value->getInt();
    return true;
}

bool JsonDecoder::decode(const char *key, uint32_t &data) {
    const Value *value = current(key);
    if (value == nullptr || !value->isUint()) { return false; }
    data = value->getUint();
    return true;
}

bool JsonDecoder::decode(const char *key, int64_t &data) {
    const Value *value = current(key);
    if (value == nullptr || !value->isInt64()) { return false; }
    data = value->getInt64();
    return true;
}

bool JsonDecoder::decode(const char *key, uint64_t &data) {
    const Value *value = current(key);
    if (value == nullptr || !value->isUint64()) { return false; }
    data = value->getUint64();
    return true;
}

bool JsonDecoder::decode(const char *key, double &data) {
    const Value *value = current(key);
    if (value == nullptr || !value->isNumber()) { return false; }
    data = value->getDouble();
    return true;
}

bool JsonDecoder::decode(const char *key, float &data) {
    const Value *value = current(key);
    if (value == nullptr || !value->isNumber()) { return false; }
    data = value->getFloat();
    return true;
}

bool JsonDecoder::decode(const char *key, bool &data) {
    const Value *value = current(key);
    if (value == nullptr || !value->isBool()) { return false; }
    data = value->getBool();
    return true;
}

bool JsonDecoder::decode(const char *key, std::string &data) {
    const Value *value = current(key);
    if (value == nullptr || !value->isString()) { return false; }
    data = value->getString();
    return true;
}

bool JsonDecoder::decode(const char *key, UUID &data) {
    const Value *value = current(key);
    if (value == nullptr || !value->isUint()) { return false; }
    data = value->getUint();
    return true;
}

bool JsonDecoder::decodeRaw(const char *key, Value &value) {
    if (m_isArray || !key) { return false; }
    const Value *found = currentObject().find(key);
    if (found == nullptr) { return false; }
    value = *found;
    return true;
}

} // namespace Rain
