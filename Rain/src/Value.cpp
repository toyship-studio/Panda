#include "Rain/Value.hpp"

#include <rapidjson/istreamwrapper.h>
#include <rapidjson/memorystream.h>
#include <rapidjson/encodedstream.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/reader.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <cstdint>
#include <istream>
#include <limits>
#include <ostream>

namespace Rain {

Value Value::array() {
    Value value;
    value.m_data = Array();
    return value;
}

Value Value::object() {
    Value value;
    value.m_data = Object();
    return value;
}

bool Value::isInt() const {
    if (const auto *u = std::get_if<uint64_t>(&m_data)) { return *u <= static_cast<uint64_t>(std::numeric_limits<int32_t>::max()); }
    if (const auto *i = std::get_if<int64_t>(&m_data)) { return *i >= std::numeric_limits<int32_t>::min(); }
    return false;
}

bool Value::isUint() const {
    const auto *u = std::get_if<uint64_t>(&m_data);
    return u != nullptr && *u <= std::numeric_limits<uint32_t>::max();
}

bool Value::isInt64() const {
    if (const auto *u = std::get_if<uint64_t>(&m_data)) { return *u <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()); }
    return std::holds_alternative<int64_t>(m_data);
}

bool Value::isUint64() const {
    return std::holds_alternative<uint64_t>(m_data);
}

bool Value::getBool() const {
    const auto *b = std::get_if<bool>(&m_data);
    return b != nullptr && *b;
}

int Value::getInt() const {
    return static_cast<int>(getInt64());
}

uint32_t Value::getUint() const {
    return static_cast<uint32_t>(getUint64());
}

int64_t Value::getInt64() const {
    if (const auto *u = std::get_if<uint64_t>(&m_data)) { return static_cast<int64_t>(*u); }
    if (const auto *i = std::get_if<int64_t>(&m_data)) { return *i; }
    if (const auto *d = std::get_if<double>(&m_data)) { return static_cast<int64_t>(*d); }
    return 0;
}

uint64_t Value::getUint64() const {
    if (const auto *u = std::get_if<uint64_t>(&m_data)) { return *u; }
    if (const auto *i = std::get_if<int64_t>(&m_data)) { return static_cast<uint64_t>(*i); }
    if (const auto *d = std::get_if<double>(&m_data)) { return static_cast<uint64_t>(*d); }
    return 0;
}

double Value::getDouble() const {
    if (const auto *d = std::get_if<double>(&m_data)) { return *d; }
    if (const auto *u = std::get_if<uint64_t>(&m_data)) { return static_cast<double>(*u); }
    if (const auto *i = std::get_if<int64_t>(&m_data)) { return static_cast<double>(*i); }
    return 0.0;
}

float Value::getFloat() const {
    return static_cast<float>(getDouble());
}

const std::string &Value::getString() const {
    static const std::string EMPTY;
    const auto *s = std::get_if<std::string>(&m_data);
    return s != nullptr ? *s : EMPTY;
}

const Value::Object &Value::members() const {
    static const Object EMPTY;
    const auto *o = std::get_if<Object>(&m_data);
    return o != nullptr ? *o : EMPTY;
}

Value::Object &Value::members() {
    if (!isObject()) { m_data = Object(); }
    return std::get<Object>(m_data);
}

const Value *Value::find(std::string_view key) const {
    for (const Member &member : members()) {
        if (member.key == key) { return &member.value; }
    }
    return nullptr;
}

Value *Value::find(std::string_view key) {
    if (!isObject()) { return nullptr; }
    for (Member &member : members()) {
        if (member.key == key) { return &member.value; }
    }
    return nullptr;
}

Value &Value::set(std::string_view key, Value value) {
    if (Value *existing = find(key)) {
        *existing = std::move(value);
        return *existing;
    }
    return add(key, std::move(value));
}

Value &Value::add(std::string_view key, Value value) {
    Object &object = members();
    object.push_back({std::string(key), std::move(value)});
    return object.back().value;
}

bool Value::erase(std::string_view key) {
    if (!isObject()) { return false; }
    Object &object = members();
    for (auto member = object.begin(); member != object.end(); ++member) {
        if (member->key == key) {
            object.erase(member);
            return true;
        }
    }
    return false;
}

const Value::Array &Value::items() const {
    static const Array EMPTY;
    const auto *a = std::get_if<Array>(&m_data);
    return a != nullptr ? *a : EMPTY;
}

Value::Array &Value::items() {
    if (!isArray()) { m_data = Array(); }
    return std::get<Array>(m_data);
}

Value &Value::push(Value value) {
    Array &array = items();
    array.push_back(std::move(value));
    return array.back();
}

size_t Value::size() const {
    if (const auto *a = std::get_if<Array>(&m_data)) { return a->size(); }
    if (const auto *o = std::get_if<Object>(&m_data)) { return o->size(); }
    return 0;
}

bool Value::operator==(const Value &other) const {
    if (isNumber() && other.isNumber()) {
        if (isDouble() || other.isDouble()) {
            const double a = getDouble();
            const double b = other.getDouble();
            return a >= b && a <= b;
        }
        // Kept canonically: a negative integer is never equal to one that is not.
        return isUint64() == other.isUint64() && getUint64() == other.getUint64();
    }
    if (m_data.index() != other.m_data.index()) { return false; }
    switch (m_data.index()) {
        case NULL_INDEX: return true;
        case BOOL_INDEX: return getBool() == other.getBool();
        case STRING_INDEX: return getString() == other.getString();
        case ARRAY_INDEX: return items() == other.items();
        default: break;
    }
    // Objects, whatever the order: each member of one is a member of the other, with an equal value.
    if (size() != other.size()) { return false; }
    for (const Member &member : members()) {
        const Value *match = other.find(member.key);
        if (match == nullptr || *match != member.value) { return false; }
    }
    return true;
}

/* ------------------------------ JSON text ------------------------------ */

namespace {

    // The events of the reader build the value: the containers being filled on a stack.
    class Builder final : public rapidjson::BaseReaderHandler<rapidjson::UTF8<>, Builder> {
    public:
        explicit Builder(Value &root)
            : m_root(root) {}

        bool Null() {
            return put(Value());
        }
        bool Bool(bool b) {
            return put(Value(b));
        }
        bool Int(int i) {
            return put(Value(i));
        }
        bool Uint(unsigned u) {
            return put(Value(u));
        }
        bool Int64(int64_t i) {
            return put(Value(i));
        }
        bool Uint64(uint64_t u) {
            return put(Value(u));
        }
        bool Double(double d) {
            return put(Value(d));
        }
        bool String(const char *text, rapidjson::SizeType length, bool) {
            return put(Value(std::string_view(text, length)));
        }
        bool StartObject() {
            return open(Value::object());
        }
        bool Key(const char *text, rapidjson::SizeType length, bool) {
            m_key.assign(text, length);
            return true;
        }
        bool EndObject(rapidjson::SizeType) {
            m_stack.pop_back();
            return true;
        }
        bool StartArray() {
            return open(Value::array());
        }
        bool EndArray(rapidjson::SizeType) {
            m_stack.pop_back();
            return true;
        }

    private:
        Value &place(Value value) {
            if (m_stack.empty()) {
                m_root = std::move(value);
                return m_root;
            }
            Value &container = *m_stack.back();
            return container.isArray() ? container.push(std::move(value)) : container.add(m_key, std::move(value));
        }
        bool put(Value value) {
            place(std::move(value));
            return true;
        }
        bool open(Value container) {
            m_stack.push_back(&place(std::move(container)));
            return true;
        }

        Value &m_root;
        std::vector<Value *> m_stack;
        std::string m_key;
    };

    template<typename Stream>
    bool parse(Stream &stream, Value &value, JsonSyntax syntax) {
        Value parsed;
        Builder builder(parsed);
        rapidjson::Reader reader;
        constexpr unsigned RELAXED = rapidjson::kParseCommentsFlag | rapidjson::kParseTrailingCommasFlag;
        const bool failed = syntax == JsonSyntax::Relaxed ? reader.Parse<RELAXED>(stream, builder).IsError()
                                                          : reader.Parse<rapidjson::kParseDefaultFlags>(stream, builder).IsError();
        if (failed) { return false; }
        value = std::move(parsed);
        return true;
    }

    // Like rapidjson's Accept: a value the writer refuses (a NaN) stops the writing.
    template<typename Writer>
    bool write(const Value &value, Writer &writer) {
        if (value.isNull()) { return writer.Null(); }
        if (value.isBool()) { return writer.Bool(value.getBool()); }
        if (value.isDouble()) { return writer.Double(value.getDouble()); }
        if (value.isUint64()) { return writer.Uint64(value.getUint64()); }
        if (value.isNumber()) { return writer.Int64(value.getInt64()); }
        if (value.isString()) {
            const std::string &text = value.getString();
            return writer.String(text.data(), static_cast<rapidjson::SizeType>(text.size()));
        }
        if (value.isArray()) {
            if (!writer.StartArray()) { return false; }
            for (const Value &item : value.items()) {
                if (!write(item, writer)) { return false; }
            }
            return writer.EndArray(static_cast<rapidjson::SizeType>(value.size()));
        }
        if (!writer.StartObject()) { return false; }
        for (const Value::Member &member : value.members()) {
            if (!writer.Key(member.key.data(), static_cast<rapidjson::SizeType>(member.key.size()))) { return false; }
            if (!write(member.value, writer)) { return false; }
        }
        return writer.EndObject(static_cast<rapidjson::SizeType>(value.size()));
    }

    template<typename Output>
    void writeTo(Output &output, const Value &value, JsonLayout layout) {
        if (layout == JsonLayout::Compact) {
            rapidjson::Writer<Output> writer(output);
            write(value, writer);
            return;
        }
        rapidjson::PrettyWriter<Output> writer(output);
        if (layout == JsonLayout::PrettyFlatArrays) { writer.SetFormatOptions(rapidjson::kFormatSingleLineArray); }
        write(value, writer);
    }

} // namespace

bool parseJson(std::string_view text, Value &value, JsonSyntax syntax) {
    rapidjson::MemoryStream memory(text.data(), text.size());
    rapidjson::EncodedInputStream<rapidjson::UTF8<>, rapidjson::MemoryStream> stream(memory);
    return parse(stream, value, syntax);
}

bool parseJson(std::istream &input, Value &value, JsonSyntax syntax) {
    rapidjson::IStreamWrapper stream(input);
    return parse(stream, value, syntax);
}

std::string writeJson(const Value &value, JsonLayout layout) {
    rapidjson::StringBuffer buffer;
    writeTo(buffer, value, layout);
    return std::string(buffer.GetString(), buffer.GetSize());
}

void writeJson(std::ostream &output, const Value &value, JsonLayout layout) {
    rapidjson::OStreamWrapper stream(output);
    writeTo(stream, value, layout);
}

} // namespace Rain
