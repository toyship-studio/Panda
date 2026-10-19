#pragma once

#include <Rain/Rain.hpp>
#include <glm/glm.hpp>

// Rain coders for glm types: a vector and a matrix are a flat array of components (a matrix —
// by columns, as it lies in memory). Needed so that glm fields can be declared in RAIN_FIELDS:
// the UBO structs of Renderer/ShaderInterface/ShaderBlocks.hpp give the Rain field metadata to
// the HLSL generator.
namespace Rain {

template<glm::length_t L, typename T, glm::qualifier Q>
struct TypeEncoder<glm::vec<L, T, Q>> {
    static void encode(const char *key, Encoder *encoder, const TypeInfo &, const glm::vec<L, T, Q> &data) {
        encoder->beginArray(key);
        for (glm::length_t i = 0; i < L; i++) {
            encoder->encode(nullptr, data[i]);
        }
        encoder->endArray();
    }
};

template<glm::length_t L, typename T, glm::qualifier Q>
struct TypeDecoder<glm::vec<L, T, Q>> {
    static bool decode(const char *key, Decoder *decoder, const TypeInfo &, glm::vec<L, T, Q> &data) {
        if (!decoder->beginArray(key)) { return false; }
        bool ok = true;
        for (glm::length_t i = 0; i < L && decoder->arrayHasElement(); i++) {
            ok = decoder->decode(nullptr, data[i]) && ok;
            decoder->arrayNext();
        }
        decoder->endArray();
        return ok;
    }
};

template<glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
struct TypeEncoder<glm::mat<C, R, T, Q>> {
    static void encode(const char *key, Encoder *encoder, const TypeInfo &, const glm::mat<C, R, T, Q> &data) {
        encoder->beginArray(key);
        for (glm::length_t column = 0; column < C; column++) {
            for (glm::length_t row = 0; row < R; row++) {
                encoder->encode(nullptr, data[column][row]);
            }
        }
        encoder->endArray();
    }
};

template<glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
struct TypeDecoder<glm::mat<C, R, T, Q>> {
    static bool decode(const char *key, Decoder *decoder, const TypeInfo &, glm::mat<C, R, T, Q> &data) {
        if (!decoder->beginArray(key)) { return false; }
        bool ok = true;
        for (glm::length_t column = 0; column < C; column++) {
            for (glm::length_t row = 0; row < R && decoder->arrayHasElement(); row++) {
                ok = decoder->decode(nullptr, data[column][row]) && ok;
                decoder->arrayNext();
            }
        }
        decoder->endArray();
        return ok;
    }
};

} // namespace Rain
