#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace Rain {

// A value of any shape — what a JSON document holds: null, a boolean, a number, a string, an
// array of values, an object of named values in the order they were put in. The form data takes
// where its C++ type is not at hand: compared, patched, kept for a type that is not registered.
// The JSON coders of Rain code typed data to and from it (JsonEncoder::encodeValue,
// JsonDecoder::decodeValue); JSON text is a value written out (parseJson, writeJson).
//
// A number is an integer or a floating-point one, as it was given or written; isInt and the
// others say whether it fits the type, the way the JSON coders read it.
class Value final {
public:
    struct Member;
    using Array = std::vector<Value>;
    using Object = std::vector<Member>;

    // (Defined below: a member of an object is a Value, so the containers are complete there.)
    Value() = default; // null
    Value(std::nullptr_t);
    Value(bool value);
    template<typename T>
        requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
    Value(T value);
    Value(float value);
    Value(double value);
    Value(const char *value);
    Value(std::string value);
    Value(std::string_view value);
    // A value moved from is null; a value may take one of its own members or items (copied or
    // moved): what it held goes only after.
    Value(const Value &other) = default;
    Value(Value &&other) noexcept;
    Value &operator=(const Value &other);
    Value &operator=(Value &&other) noexcept;
    ~Value() = default;
    static Value array();
    static Value object();

    bool isNull() const {
        return m_data.index() == NULL_INDEX;
    }
    bool isBool() const {
        return m_data.index() == BOOL_INDEX;
    }
    bool isNumber() const {
        return isInteger() || isDouble();
    }
    bool isDouble() const {
        return m_data.index() == DOUBLE_INDEX;
    }
    bool isString() const {
        return m_data.index() == STRING_INDEX;
    }
    bool isArray() const {
        return m_data.index() == ARRAY_INDEX;
    }
    bool isObject() const {
        return m_data.index() == OBJECT_INDEX;
    }
    // An integer that fits the type (a floating-point number never does).
    bool isInt() const;
    bool isUint() const;
    bool isInt64() const;
    bool isUint64() const;

    // The value as the type; what is not one (a string asked for a number) is false, 0, "".
    bool getBool() const;
    int getInt() const;
    uint32_t getUint() const;
    int64_t getInt64() const;
    uint64_t getUint64() const;
    double getDouble() const; // an integer converted
    float getFloat() const;
    const std::string &getString() const;

    // An object: its members in their order. The non-const form makes a value that is not an
    // object an empty one.
    const Object &members() const;
    Object &members();
    // The first member of the key; nullptr — none (or not an object).
    const Value *find(std::string_view key) const;
    Value *find(std::string_view key);
    // The member of the key takes the value; a key it does not have is added at the end.
    Value &set(std::string_view key, Value value);
    // Added at the end, whatever the object has: what a parser or an encoder meets, as it comes.
    Value &add(std::string_view key, Value value);
    // The first member of the key goes; false — there was none.
    bool erase(std::string_view key);

    // An array: its items in their order. The non-const form makes a value that is not an array
    // an empty one.
    const Array &items() const;
    Array &items();
    Value &push(Value value);

    // The members of an object, the items of an array; 0 for anything else.
    size_t size() const;
    bool empty() const {
        return size() == 0;
    }

    // Equal values: of one kind and equal, an object member by member whatever their order, an
    // array item by item. Numbers compare by value; one with a floating-point number compares as
    // floating-point.
    bool operator==(const Value &other) const;
    bool operator!=(const Value &other) const {
        return !(*this == other);
    }

private:
    static constexpr size_t NULL_INDEX = 0;
    static constexpr size_t BOOL_INDEX = 1;
    static constexpr size_t INT_INDEX = 2;  // a negative integer
    static constexpr size_t UINT_INDEX = 3; // an integer that is not negative
    static constexpr size_t DOUBLE_INDEX = 4;
    static constexpr size_t STRING_INDEX = 5;
    static constexpr size_t ARRAY_INDEX = 6;
    static constexpr size_t OBJECT_INDEX = 7;

    bool isInteger() const {
        return m_data.index() == INT_INDEX || m_data.index() == UINT_INDEX;
    }

    std::variant<std::monostate, bool, int64_t, uint64_t, double, std::string, Array, Object> m_data;
};

struct Value::Member final {
    std::string key;
    Value value;
};

inline Value::Value(std::nullptr_t) {}

inline Value::Value(bool value)
    : m_data(value) {}

template<typename T>
    requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
inline Value::Value(T value) {
    // Kept as a negative integer or as one that is not: the same number is one value.
    if constexpr (std::is_signed_v<T>) {
        if (value < 0) {
            m_data = static_cast<int64_t>(value);
            return;
        }
    }
    m_data = static_cast<uint64_t>(value);
}

inline Value::Value(float value)
    : m_data(static_cast<double>(value)) {}

inline Value::Value(double value)
    : m_data(value) {}

inline Value::Value(const char *value)
    : m_data(std::string(value)) {}

inline Value::Value(std::string value)
    : m_data(std::move(value)) {}

inline Value::Value(std::string_view value)
    : m_data(std::string(value)) {}

inline Value::Value(Value &&other) noexcept
    : m_data(std::move(other.m_data)) {
    other.m_data = std::monostate();
}

inline Value &Value::operator=(Value &&other) noexcept {
    if (this != &other) {
        Value moved(std::move(other)); // `other` may live inside this value
        m_data = std::move(moved.m_data);
    }
    return *this;
}

inline Value &Value::operator=(const Value &other) {
    if (this != &other) {
        Value copy(other);
        *this = std::move(copy);
    }
    return *this;
}

// JSON text ↔ a value.
// The syntax of the text: strict, as the files of the engine are; relaxed — comments (`//`,
// `/* */`) and a comma after the last item are taken, as a person writes a file by hand.
enum class JsonSyntax { Strict, Relaxed };
// How the text is laid out: on one line; one member or item a line, indented by four spaces; the
// same with every array on one line.
enum class JsonLayout { Compact, Pretty, PrettyFlatArrays };
// false — the text is not one JSON value; the value is left as it was.
bool parseJson(std::string_view text, Value &value, JsonSyntax syntax = JsonSyntax::Strict);
bool parseJson(std::istream &stream, Value &value, JsonSyntax syntax = JsonSyntax::Strict);
std::string writeJson(const Value &value, JsonLayout layout = JsonLayout::Compact);
void writeJson(std::ostream &stream, const Value &value, JsonLayout layout = JsonLayout::Compact);

} // namespace Rain
