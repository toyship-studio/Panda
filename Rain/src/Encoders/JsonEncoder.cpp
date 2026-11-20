#include "Rain/Coders/JsonEncoder.hpp"
#include "Rain/TypeRegistry.hpp"

#include <ostream>

namespace Rain {

JsonEncoder::JsonEncoder(bool pretty, bool encodeOptionalAsNull)
    : Encoder(encodeOptionalAsNull)
    , m_pretty(pretty)
    , m_isArray(false)
    , m_root()
    , m_objectStack()
    , m_arrayStack() {}

Value JsonEncoder::encodeValue(void *data, TypeInfo info) {
    info.encoderFunc(nullptr, this, info, data);
    Value value = std::move(m_root);
    m_root = Value();
    m_objectStack.clear();
    m_arrayStack.clear();
    m_isArray = false;
    return value;
}

void JsonEncoder::encode(std::ostream &stream, void *data, TypeInfo info) {
    writeJson(stream, encodeValue(data, info), m_pretty ? JsonLayout::Pretty : JsonLayout::Compact);
}

Value &JsonEncoder::add(const char *key, Value value) {
    if (m_isArray) { return m_arrayStack.back().value->push(std::move(value)); }
    Value &object = m_objectStack.empty() ? m_root : *m_objectStack.back().value;
    return object.add(key != nullptr ? key : "", std::move(value));
}

void JsonEncoder::beginObject(const char *key) {
    Value *added;
    if (m_isArray) {
        added = &m_arrayStack.back().value->push(Value::object());
    } else if (m_objectStack.empty()) {
        m_root = Value::object();
        added = &m_root;
    } else {
        added = &add(key, Value::object());
    }
    m_objectStack.push_back({added, m_isArray});
    m_isArray = false;
}

void JsonEncoder::endObject() {
    m_isArray = m_objectStack.back().isPartOfArray;
    m_objectStack.pop_back();
}

void JsonEncoder::beginArray(const char *key) {
    Value *added;
    if (m_isArray) {
        added = &m_arrayStack.back().value->push(Value::array());
    } else if (m_objectStack.empty()) {
        m_root = Value::array();
        added = &m_root;
    } else {
        added = &add(key, Value::array());
    }
    m_arrayStack.push_back({added, m_isArray});
    m_isArray = true;
}

void JsonEncoder::endArray() {
    m_isArray = m_arrayStack.back().isPartOfArray;
    m_arrayStack.pop_back();
}

void JsonEncoder::encodeNull(const char *key) {
    add(key, Value());
}

void JsonEncoder::encode(const char *key, const int &data) {
    add(key, Value(data));
}

void JsonEncoder::encode(const char *key, const uint32_t &data) {
    add(key, Value(data));
}

void JsonEncoder::encode(const char *key, const int64_t &data) {
    add(key, Value(data));
}

void JsonEncoder::encode(const char *key, const uint64_t &data) {
    add(key, Value(data));
}

void JsonEncoder::encode(const char *key, const bool &data) {
    add(key, Value(data));
}

void JsonEncoder::encode(const char *key, const float &data) {
    add(key, Value(data));
}

void JsonEncoder::encode(const char *key, const double &data) {
    add(key, Value(data));
}

void JsonEncoder::encode(const char *key, const std::string &data) {
    add(key, Value(data));
}

void JsonEncoder::encode(const char *key, const char *&data) {
    add(key, Value(data != nullptr ? data : ""));
}

void JsonEncoder::encode(const char *key, const UUID &data) {
    UUID id = data;
    add(key, Value(static_cast<uint32_t>(id)));
}

void JsonEncoder::encodeRaw(const char *key, const Value &value) {
    add(key, value);
}

} // namespace Rain
