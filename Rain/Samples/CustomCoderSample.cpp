#include "Samples.hpp"

#include "Rain/Codable.hpp"
#include "Rain/Coders/JsonDecoder.hpp"
#include "Rain/Coders/JsonEncoder.hpp"

#include <iostream>
#include <sstream>
#include <string>

namespace RainSamples::CustomCoderSample {

enum class WeatherKind { Clear, Rain, Fog };

const char *weatherToString(WeatherKind weather) {
    switch (weather) {
        case WeatherKind::Clear:
            return "Clear";
        case WeatherKind::Rain:
            return "Rain";
        case WeatherKind::Fog:
            return "Fog";
    }
    return "Clear";
}

WeatherKind weatherFromString(const std::string &weather) {
    if (weather == "Rain") {
        return WeatherKind::Rain;
    }
    if (weather == "Fog") {
        return WeatherKind::Fog;
    }
    return WeatherKind::Clear;
}

struct WeatherState : public Rain::Codable {
    WeatherKind kind = WeatherKind::Clear;
    float intensity = 0.0f;

    static void encode(const char *key, Rain::Encoder *encoder, const WeatherState &data) {
        encoder->beginObject(key);
        const char *kind = weatherToString(data.kind);
        encoder->encode("kind", kind);
        encoder->encode("intensity", data.intensity);
        encoder->endObject();
    }

    static bool decode(const char *key, Rain::Decoder *decoder, WeatherState &data) {
        if (!decoder->beginObject(key)) {
            return false;
        }

        std::string kind;
        bool ok = decoder->decode("kind", kind);
        ok = decoder->decode("intensity", data.intensity) && ok;
        data.kind = weatherFromString(kind);
        decoder->endObject();
        return ok;
    }
};

struct WeatherProfile : public Rain::Codable {
    std::string name;
    WeatherState weather;

    RAIN_FIELDS_BEGIN(WeatherProfile)
    RAIN_FIELD(name)
    RAIN_FIELD(weather)
    RAIN_FIELDS_END
};

} // namespace RainSamples::CustomCoderSample

namespace RainSamples {

bool sampleCustomCoder() {
    std::cout << "\n=== Custom coder ===\n";

    CustomCoderSample::WeatherProfile source;
    source.name = "MorningFog";
    source.weather.kind = CustomCoderSample::WeatherKind::Fog;
    source.weather.intensity = 0.5f;

    std::stringstream stream;
    Rain::JsonEncoder encoder(true);
    encodeTo(encoder, stream, source);

    std::cout << stream.str() << "\n";

    CustomCoderSample::WeatherProfile decoded;
    std::stringstream input(stream.str());
    Rain::JsonDecoder decoder;
    if (!decodeFrom(decoder, input, decoded)) {
        std::cerr << "Failed to decode custom weather profile\n";
        return false;
    }

    std::cout << "Weather decoded as string enum: "
              << CustomCoderSample::weatherToString(decoded.weather.kind) << ", intensity "
              << decoded.weather.intensity << "\n";
    return true;
}

} // namespace RainSamples
