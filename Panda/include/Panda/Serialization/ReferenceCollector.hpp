#pragma once

#include <Rain/Rain.hpp>

#include <ostream>
#include <unordered_set>

namespace Panda {

// An encoder that writes nothing and keeps the ids that pass through it: what a world, a
// material or the file of a module names. A reference to an asset is always written as an id
// (AssetRef, a UUID field of a DTO), so encoding a document through the collector lists what the
// document needs — whatever its type, a component of a module included, with no code per type.
// An entity is named by an id too: whoever asks keeps the ids that are assets.
class ReferenceCollector final : public Rain::Encoder {
public:
    using Rain::Encoder::encode;

    ReferenceCollector()
        : Rain::Encoder(false) {}

    const std::unordered_set<Rain::UUID> &ids() const {
        return m_ids;
    }
    void clear() {
        m_ids.clear();
    }

    void encode(std::ostream &, void *data, Rain::TypeInfo info) override {
        info.encoderFunc(nullptr, this, info, data);
    }
    // It takes the values a format of registered data cannot carry (the components of a module
    // that is not loaded) and looks into none of them: what such a module names, the game does
    // not use.
    bool supportsRawJson() const override {
        return true;
    }
    void encode(const char *, const Rain::UUID &data) override {
        m_ids.insert(data);
    }
    void encodeNull(const char *) override {}
    void beginObject(const char *) override {}
    void encode(const char *, const bool &) override {}
    void encode(const char *, const int &) override {}
    void encode(const char *, const uint32_t &) override {}
    void encode(const char *, const int64_t &) override {}
    void encode(const char *, const uint64_t &) override {}
    void encode(const char *, const float &) override {}
    void encode(const char *, const double &) override {}
    void encode(const char *, const std::string &) override {}
    void encode(const char *, const char *&) override {}
    void encodeRawJson(const char *, const std::string &) override {}
    void endObject() override {}
    void beginArray(const char *) override {}
    void endArray() override {}

private:
    std::unordered_set<Rain::UUID> m_ids;
};

} // namespace Panda
