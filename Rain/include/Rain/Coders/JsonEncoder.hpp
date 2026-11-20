#pragma once

#include "Rain/Coder.hpp"
#include "Rain/TypeRegistry.hpp"
#include "Rain/Value.hpp"

#include <cstdint>
#include <vector>

namespace Rain {

// Data → JSON: a Value (encodeValue), or its text (encode into a stream).
class JsonEncoder final : public Encoder {
public:
    using Encoder::encode;

    explicit JsonEncoder(bool pretty = false, bool encodeOptionalAsNull = false);
    ~JsonEncoder() override = default;

    // The data as a JSON value: what the text of `encode` is written from.
    template<typename T>
    Value encodeValue(const T &data) {
        TypeInfo info = getTypeRegistry()->findOrCreateType<T>();
        return encodeValue(const_cast<T *>(&data), info);
    }
    Value encodeValue(void *data, TypeInfo info);

    void encode(std::ostream &, void *data, TypeInfo info) override;
    void beginObject(const char *key) override;
    void encodeNull(const char *key) override;
    void encode(const char *key, const bool &data) override;
    void encode(const char *key, const int &data) override;
    void encode(const char *key, const uint32_t &data) override;
    void encode(const char *key, const int64_t &data) override;
    void encode(const char *key, const uint64_t &data) override;
    void encode(const char *key, const float &data) override;
    void encode(const char *key, const double &data) override;
    void encode(const char *key, const std::string &data) override;
    void encode(const char *key, const char *&data) override;
    void encode(const char *key, const UUID &data) override;
    void encodeRaw(const char *key, const Value &value) override;
    bool supportsRaw() const override {
        return true;
    }
    void endObject() override;
    void beginArray(const char *key) override;
    void endArray() override;

private:
    // A container being filled: an object or an array, and whether it is an item of an array.
    struct Level {
        Value *value;
        bool isPartOfArray;
    };

    Value &add(const char *key, Value value);

    bool m_pretty;
    bool m_isArray;
    Value m_root;
    std::vector<Level> m_objectStack;
    std::vector<Level> m_arrayStack;
};

} // namespace Rain
