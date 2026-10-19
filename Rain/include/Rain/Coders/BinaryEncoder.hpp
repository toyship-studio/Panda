#pragma once

#include "Rain/Coder.hpp"
#include "Rain/Coders/BinaryCoder.hpp"

namespace Rain {

class BinaryEncoder final : public Encoder {
public:
    using Encoder::encode;

    explicit BinaryEncoder(bool encodeOptionalAsNull = false);
    ~BinaryEncoder() override = default;

    void encode(std::ostream &, void *data, TypeInfo info) override;
    void encodeNull(const char *key) override;
    void beginObject(const char *key) override;
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
    void encodeRawJson(const char *key, const std::string &json) override;
    void endObject() override;
    void beginArray(const char *key) override;
    void endArray() override;

private:
    std::unique_ptr<BinaryNode> m_root;
    std::vector<BinaryNode *> m_containerStack;

    BinaryNode *addNode(const char *key, std::unique_ptr<BinaryNode> node);
};

} // namespace Rain
