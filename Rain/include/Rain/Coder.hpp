#pragma once

#include "TypeInfo.hpp"
#include "UUID.hpp"

#include <cstdint>
#include <istream>
#include <ostream>
#include <filesystem>

namespace Rain {

class Codable;

enum class MissingFieldPolicy {
    Fail,
    KeepCurrentValue,
    DefaultConstruct,
};

struct DecoderSettings {
    MissingFieldPolicy missingFieldPolicy = MissingFieldPolicy::KeepCurrentValue;
};

class Encoder {
public:
    Encoder(bool encodeOptionalAsNull)
        : m_encodeOptionalAsNull(encodeOptionalAsNull) {}
    virtual ~Encoder() = default;

    template<typename T>
    void encode(std::ostream &stream, const T &data);

    template<typename T>
        requires std::derived_from<T, Codable>
    void encode(const char *key, const T &data);

    template<typename T>
        requires std::is_enum_v<T>
    void encode(const char *key, const T &data);

    bool encodeOptionalAsNull() {
        return m_encodeOptionalAsNull;
    }

    /// General encode
    virtual void encode(std::ostream &, void *data, TypeInfo info) = 0;
    /// Encode null
    virtual void encodeNull(const char *key) = 0;
    /// Begin encoding object
    virtual void beginObject(const char *key) = 0;
    /// Encode bool
    virtual void encode(const char *key, const bool &data) = 0;
    /// Encode int
    virtual void encode(const char *key, const int &data) = 0;
    /// Encode uint
    virtual void encode(const char *key, const uint32_t &data) = 0;
    /// Encode int64
    virtual void encode(const char *key, const int64_t &data) = 0;
    /// Encode uint64
    virtual void encode(const char *key, const uint64_t &data) = 0;
    /// Encode float
    virtual void encode(const char *key, const float &data) = 0;
    /// Encode double
    virtual void encode(const char *key, const double &data) = 0;
    /// Encode string
    virtual void encode(const char *key, const std::string &data) = 0;
    /// Encode const char*
    virtual void encode(const char *key, const char *&data) = 0;
    /// Encode uuid
    virtual void encode(const char *key, const UUID &data) = 0;
    /// Opaque value — JSON text under a key (data whose type is unknown to the host: components
    /// and settings of modules that are not loaded). Only the JSON coders carry it (the fragment
    /// becomes a value of the document); the others drop it — the caller checks supportsRawJson
    /// and decides what to say about the loss.
    virtual void encodeRawJson(const char *key, const std::string &json) = 0;
    virtual bool supportsRawJson() const {
        return false;
    }
    /// End encoding object
    virtual void endObject() = 0;
    /// Begin encoding array
    virtual void beginArray(const char *key) = 0;
    /// End encoding array
    virtual void endArray() = 0;

private:
    bool m_encodeOptionalAsNull = false;

    friend class TypeRegistry;
};

class Decoder {
public:
    explicit Decoder(DecoderSettings settings = {})
        : m_settings(settings) {}
    virtual ~Decoder() = default;

    template<typename T>
    bool decode(std::istream &stream, T &data);

    template<typename T>
        requires std::derived_from<T, Codable>
    bool decode(const char *key, T &data);

    template<typename T>
        requires std::is_enum_v<T>
    bool decode(const char *key, T &data);

    /// General decode
    virtual bool decode(std::istream &, void *data, TypeInfo info) = 0;
    /// Check if current object contains a member with the given key
    virtual bool hasMember(const char *key) = 0;
    /// Get all keys on current level
    virtual std::vector<std::string> getMemberKeys() = 0;
    /// Begin decoding object
    virtual bool beginObject(const char *key) = 0;
    /// Decode bool
    virtual bool decode(const char *key, bool &data) = 0;
    /// Decode int
    virtual bool decode(const char *key, int &data) = 0;
    /// Decode uint
    virtual bool decode(const char *key, uint32_t &data) = 0;
    /// Decode int64
    virtual bool decode(const char *key, int64_t &data) = 0;
    /// Decode uint64
    virtual bool decode(const char *key, uint64_t &data) = 0;
    /// Decode float
    virtual bool decode(const char *key, float &data) = 0;
    /// Decode double
    virtual bool decode(const char *key, double &data) = 0;
    /// Decode string
    virtual bool decode(const char *key, std::string &data) = 0;
    /// Decode uuid
    virtual bool decode(const char *key, UUID &data) = 0;
    /// Opaque value under a key as JSON text (the counterpart of Encoder::encodeRawJson); false
    /// where the format cannot carry one (supportsRawJson).
    virtual bool decodeRawJson(const char *key, std::string &json) = 0;
    virtual bool supportsRawJson() const {
        return false;
    }
    /// End decoding object
    virtual void endObject() = 0;
    /// Begin decoding array
    virtual bool beginArray(const char *key) = 0;
    /// Check if array has current element
    virtual bool arrayHasElement() = 0;
    /// Go to next array element
    virtual void arrayNext() = 0;
    /// End decoding array
    virtual void endArray() = 0;

    const DecoderSettings &getSettings() const {
        return m_settings;
    }

private:
    DecoderSettings m_settings;
};

} // namespace Rain
