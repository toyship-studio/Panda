#pragma once

#include "Rain/Coder.hpp"
#include "Rain/Coders/BinaryCoder.hpp"

#include <cstddef>

namespace Rain {

class BinaryDecoder final : public Decoder {
    struct Object {
        BinaryNode *ref;
        bool isPartOfArray;
    };

    struct Array {
        BinaryNode *ref;
        size_t index;
        bool isPartOfArray;
    };

public:
    using Decoder::decode;

    explicit BinaryDecoder(DecoderSettings settings = {});
    ~BinaryDecoder() override = default;

    bool decode(std::istream &, void *data, TypeInfo info) override;
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
    bool decodeRawJson(const char *key, std::string &json) override;
    void endObject() override;
    bool beginArray(const char *key) override;
    bool arrayHasElement() override;
    void arrayNext() override;
    void endArray() override;

private:
    std::unique_ptr<BinaryNode> m_root;
    bool m_isArray;
    std::vector<Object> m_objectStack;
    std::vector<Array> m_arrayStack;

    BinaryNode *currentObject();
    BinaryNode *currentArrayElement();
    BinaryNode *findMember(BinaryNode *object, const char *key);
    BinaryNode *nodeForKey(const char *key);
};

} // namespace Rain
