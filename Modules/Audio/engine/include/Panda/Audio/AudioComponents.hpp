#pragma once

#include "Panda/Audio/AudioEngine.hpp"

#include <Panda/Assets/Base/AssetRef.hpp>

#include <string>

namespace Panda::Audio {

// A sound of an entity: the clip and how it is played. The component is authored data (the key
// audioSourceComponent of .world); the voice it plays through is runtime state of the audio world
// system (World::context<AudioWorldState>), created at the start of the running world
// (playOnStart) or by a script, and following the component every frame — the component is the
// truth, an inspector edit or an undo reaches the voice without a hook.
struct AudioSourceComponent final {
    AssetRef<Asset> clip; // an AudioClipAsset
    std::string bus = "sfx";
    float volume = 1.f;
    float pitch = 1.f;
    bool loop = false;
    bool playOnStart = false;
    bool spatial = true; // attenuated and panned by the entity's position relative to the listener
    float minDistance = 1.f;
    float maxDistance = 50.f;
    AttenuationModel attenuation = AttenuationModel::Inverse;
    float dopplerFactor = 0.f; // 0 — no Doppler shift; the velocity comes from the entity's motion
};

// The entity spatial voices are heard from (the key audioListenerComponent of .world): its world
// position and orientation (forward is −Z, up is +Y, as for a camera). A world without a listener
// hears from the main camera.
struct AudioListenerComponent final {};

} // namespace Panda::Audio
