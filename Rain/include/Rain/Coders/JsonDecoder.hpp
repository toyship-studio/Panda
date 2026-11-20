#pragma once

#include "Rain/Coder.hpp"
#include "Rain/TypeRegistry.hpp"
#include "Rain/Value.hpp"

#include <cstdint>
#include <vector>

namespace Rain {

// JSON → data: from a Value (decodeValue), or from its text (decode from a stream).
class JsonDecoder final : public Decoder {
public:
    using Decoder::decode;

    explicit JsonDecoder(DecoderSettings settings = {});
    ~JsonDecoder() override = default;

    // The data from a JSON value; false — the value is not what the data reads from.
    template<typename T>
    bool decodeValue(const Value &value, T &data) {
        TypeInfo info = getTypeRegistry()->findOrCreateType<T>();
        return decodeValue(value, &data, info);
    }
    bool decodeValue(const Value &value, void *data, TypeInfo info);

    bool decode(std::istream &istream, void *data, TypeInfo info) override;
    bool hasMember(const char *key) override;
    std::vector<std::string> getMemberKeys() override;
    bool beginObject(const char *key) override;
    bool decode(const char *key, bool &data) override;
    bool decode(const char *key, int &data) override;
    bool decode(const char *key, uint32_t &data) override;
    bool decode(const char *key, int64_t &data) override;
    bool decode(const char *key, uint64_t &data) override;
    bool decode(const char *key, float &data) override;
    bool decode(const char *key, double &data) override;
    bool decode(const char *key, std::string &data) override;
    bool decode(const char *key, UUID &data) override;
    bool decodeRaw(const char *key, Value &value) override;
    bool supportsRaw() const override {
        return true;
    }
    void endObject() override;
    bool beginArray(const char *key) override;
    bool arrayHasElement() override;
    void arrayNext() override;
    void endArray() override;

private:
    struct Object {
        const Value *value;
        bool isPartOfArray;
    };
    struct Array {
        const Value *value;
        size_t index; // the item being read
        bool isPartOfArray;
    };

    const Value &currentObject() const;
    // What is read next: the item of the array being read, or the member of the key.
    const Value *current(const char *key) const;

    bool m_isArray;
    const Value *m_root;
    std::vector<Object> m_objectStack;
    std::vector<Array> m_arrayStack;
};

} // namespace Rain
