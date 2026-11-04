#pragma once

#include <Panda/Base/UUID.hpp>
#include <Rain/Rain.hpp>

#include <cstdint>
#include <string>

namespace Panda::Audio {

// The .world shape of the audio source (the key audioSourceComponent; C++ field name = JSON key).
// The defaults mirror the component; attenuation is the number of AttenuationModel.
struct AudioSourceComponentDto final : public Rain::Codable {
    UUID clip = 0;
    std::string bus = "sfx";
    float volume = 1.f;
    float pitch = 1.f;
    bool loop = false;
    bool playOnStart = false;
    bool spatial = true;
    float minDistance = 1.f;
    float maxDistance = 50.f;
    uint32_t attenuation = 1;
    float dopplerFactor = 0.f;

    RAIN_FIELDS_BEGIN(AudioSourceComponentDto)
    RAIN_FIELD(clip)
    RAIN_FIELD(bus)
    RAIN_FIELD(volume)
    RAIN_FIELD(pitch)
    RAIN_FIELD(loop)
    RAIN_FIELD(playOnStart)
    RAIN_FIELD(spatial)
    RAIN_FIELD(minDistance)
    RAIN_FIELD(maxDistance)
    RAIN_FIELD(attenuation)
    RAIN_FIELD(dopplerFactor)
    RAIN_FIELDS_END
};

} // namespace Panda::Audio
