#include "Panda/Audio/AudioEngine.hpp"

#include "AudioClipProbe.hpp"

#include <Foundation/Foundation.hpp>

#include <miniaudio/miniaudio.h>

#include <algorithm>
#include <cstdio>
#include <thread>
#include <unordered_map>

// The only translation units of the engine that see miniaudio are this one and the clip probe:
// the public headers of the module carry no ma_* type (context/audio.md), so the backend can be
// swapped inside the module.

namespace Panda::Audio {

namespace {

    AudioEngine *s_instance = nullptr;
    std::thread::id s_mainThread;

    bool onMainThread() {
        return std::this_thread::get_id() == s_mainThread;
    }

    // The registration name of a clip's bytes with the resource manager: by the identity of the
    // shared buffer — a reloaded asset has a new buffer and a new name, the old one stays
    // registered until its last voice is done.
    std::string clipName(const AudioClipAsset::Bytes *bytes) {
        char name[32];
        std::snprintf(name, sizeof(name), "clip:%p", static_cast<const void *>(bytes));
        return name;
    }

    ma_attenuation_model toMiniaudio(AttenuationModel model) {
        switch (model) {
            case AttenuationModel::None:
                return ma_attenuation_model_none;
            case AttenuationModel::Linear:
                return ma_attenuation_model_linear;
            case AttenuationModel::Exponential:
                return ma_attenuation_model_exponential;
            case AttenuationModel::Inverse:
            default:
                return ma_attenuation_model_inverse;
        }
    }

    // A started voice gets a serial: the oldest one-shot is the one stolen when the pool is full.
    uint64_t s_nextSerial = 1;

} // namespace

bool probeClip(const uint8_t *data, size_t size, AudioClipInfo &info) {
    ma_decoder decoder;
    if (ma_decoder_init_memory(data, size, nullptr, &decoder) != MA_SUCCESS) { return false; }
    ma_format format;
    ma_uint32 channels = 0;
    ma_uint32 sampleRate = 0;
    const bool ok = ma_decoder_get_data_format(&decoder, &format, &channels, &sampleRate, nullptr, 0) == MA_SUCCESS;
    ma_uint64 frames = 0;
    if (ok) { ma_decoder_get_length_in_pcm_frames(&decoder, &frames); } // 0 when the stream has no length
    ma_decoder_uninit(&decoder);
    if (!ok || channels == 0 || sampleRate == 0) { return false; }
    info.channels = channels;
    info.sampleRate = sampleRate;
    info.frameCount = frames;
    return true;
}

struct AudioEngine::Impl final {
    struct Voice final {
        ma_sound sound{};
        bool used = false;
        bool paused = false;
        bool stopping = false; // a fade-out is scheduled: released when the sound has stopped
        bool loop = false;
        uint32_t generation = 1; // grows on release: the handle of the previous sound goes stale
        uint64_t serial = 0;
        const AudioClipAsset::Bytes *clip = nullptr; // the cache entry the voice plays from
    };

    // A clip registered with the resource manager: the engine holds the buffer as long as the
    // entry lives, so a reloaded or unloaded asset does not pull the bytes from under a voice.
    struct CachedClip final {
        std::shared_ptr<const AudioClipAsset::Bytes> bytes;
        std::string name;
    };

    ma_context nullContext{};
    bool ownsContext = false;
    ma_engine engine{};
    bool ready = false;
    bool deviceActive = false;
    std::array<ma_sound_group, BUS_NAMES.size()> groups{}; // [0] (master) is unused: the master is the engine
    std::array<float, BUS_NAMES.size()> busVolumes{};
    bool muted = false;
    std::array<Voice, VOICE_CAPACITY> voices{};
    std::unordered_map<const AudioClipAsset::Bytes *, CachedClip> cache;

    // --- Lifecycle ---

    bool init() {
        busVolumes.fill(1.f);
        ma_engine_config config = ma_engine_config_init();
        ma_result result = ma_engine_init(&config, &engine);
        if (result != MA_SUCCESS) {
            // No playback device (a CI runner, a headless machine): the null backend keeps the
            // clock running so that voices finish and the API behaves; nothing is heard.
            LOG_ERROR("AUDIO: no playback device (%s); running on the null backend", ma_result_description(result));
            const ma_backend backends[] = {ma_backend_null};
            ma_context_config contextConfig = ma_context_config_init();
            if (ma_context_init(backends, 1, &contextConfig, &nullContext) != MA_SUCCESS) {
                LOG_ERROR("AUDIO: the null backend failed as well; audio is off");
                return false;
            }
            ownsContext = true;
            config.pContext = &nullContext;
            result = ma_engine_init(&config, &engine);
            if (result != MA_SUCCESS) {
                LOG_ERROR("AUDIO: engine init failed (%s); audio is off", ma_result_description(result));
                ma_context_uninit(&nullContext);
                ownsContext = false;
                return false;
            }
        } else {
            deviceActive = true;
        }
        for (size_t i = 1; i < BUS_NAMES.size(); ++i) {
            if (ma_sound_group_init(&engine, 0, nullptr, &groups[i]) != MA_SUCCESS) {
                LOG_ERROR("AUDIO: bus %s failed to initialize", BUS_NAMES[i]);
            }
        }
        ready = true;
        LOG_INFO(
            "AUDIO: mixer up — %u Hz, %u channels, %s", ma_engine_get_sample_rate(&engine), ma_engine_get_channels(&engine),
            deviceActive ? "playback device" : "null backend"
        );
        return true;
    }

    void deinit() {
        if (!ready) { return; }
        for (Voice &voice : voices) {
            if (voice.used) { release(voice); }
        }
        for (auto &entry : cache) { ma_resource_manager_unregister_data(ma_engine_get_resource_manager(&engine), entry.second.name.c_str()); }
        cache.clear();
        for (size_t i = 1; i < BUS_NAMES.size(); ++i) { ma_sound_group_uninit(&groups[i]); }
        ma_engine_uninit(&engine);
        if (ownsContext) { ma_context_uninit(&nullContext); }
        ownsContext = false;
        ready = false;
    }

    // --- Buses ---

    int busIndex(std::string_view name) const {
        for (size_t i = 0; i < BUS_NAMES.size(); ++i) {
            if (name == BUS_NAMES[i]) { return static_cast<int>(i); }
        }
        return -1;
    }

    ma_sound_group *groupOf(std::string_view name) {
        int index = busIndex(name);
        if (index <= 0) { index = busIndex("sfx"); } // the master takes no voices directly; unknown names play on sfx
        return &groups[static_cast<size_t>(index)];
    }

    void applyMaster() {
        ma_engine_set_volume(&engine, muted ? 0.f : busVolumes[0]);
    }

    // --- Voices ---

    Voice *resolve(VoiceHandle handle) {
        if (handle.index >= VOICE_CAPACITY) { return nullptr; }
        Voice &voice = voices[handle.index];
        return voice.used && voice.generation == handle.generation ? &voice : nullptr;
    }
    const Voice *resolve(VoiceHandle handle) const {
        return const_cast<Impl *>(this)->resolve(handle);
    }

    Voice *acquire() {
        for (Voice &voice : voices) {
            if (!voice.used) { return &voice; }
        }
        // Full: steal the oldest one-shot (a looping voice is somebody's ambience or music).
        Voice *oldest = nullptr;
        for (Voice &voice : voices) {
            if (voice.loop) { continue; }
            if (oldest == nullptr || voice.serial < oldest->serial) { oldest = &voice; }
        }
        if (oldest == nullptr) { return nullptr; }
        release(*oldest);
        return oldest;
    }

    void release(Voice &voice) {
        ma_sound_uninit(&voice.sound);
        voice.used = false;
        voice.paused = false;
        voice.stopping = false;
        voice.loop = false;
        voice.clip = nullptr;
        voice.generation++;
    }

    const CachedClip &registerClip(const AudioClipAsset &clip) {
        const AudioClipAsset::Bytes *key = clip.bytes().get();
        auto it = cache.find(key);
        if (it != cache.end()) { return it->second; }
        CachedClip entry;
        entry.bytes = clip.bytes();
        entry.name = clipName(key);
        ma_resource_manager_register_encoded_data(
            ma_engine_get_resource_manager(&engine), entry.name.c_str(), entry.bytes->data(), entry.bytes->size()
        );
        return cache.emplace(key, std::move(entry)).first->second;
    }

    void applyParams(Voice &voice, const VoiceParams &params) {
        ma_sound_set_volume(&voice.sound, std::max(params.volume, 0.f));
        ma_sound_set_pitch(&voice.sound, std::max(params.pitch, 0.01f));
        ma_sound_set_looping(&voice.sound, params.loop ? MA_TRUE : MA_FALSE);
        voice.loop = params.loop;
        ma_sound_set_spatialization_enabled(&voice.sound, params.spatial ? MA_TRUE : MA_FALSE);
        if (params.spatial) {
            ma_sound_set_position(&voice.sound, params.position.x, params.position.y, params.position.z);
            ma_sound_set_velocity(&voice.sound, params.velocity.x, params.velocity.y, params.velocity.z);
            ma_sound_set_attenuation_model(&voice.sound, toMiniaudio(params.attenuation));
            ma_sound_set_min_distance(&voice.sound, std::max(params.minDistance, 0.f));
            ma_sound_set_max_distance(&voice.sound, std::max(params.maxDistance, params.minDistance));
            ma_sound_set_rolloff(&voice.sound, std::max(params.rolloff, 0.f));
            ma_sound_set_doppler_factor(&voice.sound, std::max(params.dopplerFactor, 0.f));
        }
    }

    // Finished and faded-out voices go back to the pool; cache entries nobody plays or holds are
    // dropped (the asset was reloaded or unloaded: its use of the buffer is gone).
    void collect() {
        for (Voice &voice : voices) {
            if (!voice.used || voice.paused) { continue; }
            const bool ended = ma_sound_at_end(&voice.sound) == MA_TRUE;
            const bool stopped = voice.stopping && ma_sound_is_playing(&voice.sound) == MA_FALSE;
            if (ended || stopped) { release(voice); }
        }
        for (auto it = cache.begin(); it != cache.end();) {
            const bool held = it->second.bytes.use_count() > 1;
            bool playing = false;
            for (const Voice &voice : voices) {
                if (voice.used && voice.clip == it->first) {
                    playing = true;
                    break;
                }
            }
            if (!held && !playing) {
                ma_resource_manager_unregister_data(ma_engine_get_resource_manager(&engine), it->second.name.c_str());
                it = cache.erase(it);
            } else {
                ++it;
            }
        }
    }
};

AudioEngine *AudioEngine::instance() {
    return s_instance;
}

void AudioEngine::startup() {
    if (s_instance != nullptr) { return; }
    s_mainThread = std::this_thread::get_id();
    s_instance = new AudioEngine();
}

void AudioEngine::shutdown() {
    delete s_instance;
    s_instance = nullptr;
}

AudioEngine::AudioEngine()
    : m_impl(std::make_unique<Impl>()) {
    m_impl->init();
}

AudioEngine::~AudioEngine() {
    m_impl->deinit();
}

void AudioEngine::update() {
    PND_ASSERT(onMainThread(), "AUDIO: main thread only");
    if (!m_impl->ready) { return; }
    m_impl->collect();
}

VoiceHandle AudioEngine::play(const AudioClipAsset &clip, const VoiceParams &params) {
    PND_ASSERT(onMainThread(), "AUDIO: main thread only");
    if (!m_impl->ready || clip.isEmpty()) { return {}; }
    Impl::Voice *voice = m_impl->acquire();
    if (voice == nullptr) { return {}; } // every voice is a loop: nothing to steal
    const Impl::CachedClip &cached = m_impl->registerClip(clip);
    ma_uint32 flags = 0;
    if (clip.info().durationSeconds() < DECODE_IN_MEMORY_SECONDS) { flags |= MA_SOUND_FLAG_DECODE; }
    if (!params.spatial) { flags |= MA_SOUND_FLAG_NO_SPATIALIZATION; }
    const ma_result result = ma_sound_init_from_file(
        &m_impl->engine, cached.name.c_str(), flags, m_impl->groupOf(params.bus), nullptr, &voice->sound
    );
    if (result != MA_SUCCESS) {
        LOG_ERROR("AUDIO: voice init failed (%s)", ma_result_description(result));
        return {};
    }
    voice->used = true;
    voice->serial = s_nextSerial++;
    voice->clip = cached.bytes.get();
    m_impl->applyParams(*voice, params);
    if (params.fadeInSeconds > 0.f) {
        ma_sound_set_fade_in_milliseconds(&voice->sound, 0.f, 1.f, static_cast<ma_uint64>(params.fadeInSeconds * 1000.f));
    }
    voice->paused = params.startPaused;
    if (!params.startPaused) { ma_sound_start(&voice->sound); }
    return VoiceHandle{static_cast<uint32_t>(voice - m_impl->voices.data()), voice->generation};
}

void AudioEngine::stop(VoiceHandle handle, float fadeSeconds) {
    PND_ASSERT(onMainThread(), "AUDIO: main thread only");
    Impl::Voice *voice = m_impl->resolve(handle);
    if (voice == nullptr) { return; }
    if (fadeSeconds <= 0.f || voice->paused) {
        m_impl->release(*voice);
        return;
    }
    ma_sound_stop_with_fade_in_milliseconds(&voice->sound, static_cast<ma_uint64>(fadeSeconds * 1000.f));
    voice->stopping = true;
}

void AudioEngine::setPaused(VoiceHandle handle, bool paused) {
    PND_ASSERT(onMainThread(), "AUDIO: main thread only");
    Impl::Voice *voice = m_impl->resolve(handle);
    if (voice == nullptr || voice->paused == paused || voice->stopping) { return; }
    voice->paused = paused;
    if (paused) {
        ma_sound_stop(&voice->sound);
    } else {
        ma_sound_start(&voice->sound);
    }
}

void AudioEngine::apply(VoiceHandle handle, const VoiceParams &params) {
    PND_ASSERT(onMainThread(), "AUDIO: main thread only");
    if (Impl::Voice *voice = m_impl->resolve(handle)) { m_impl->applyParams(*voice, params); }
}

void AudioEngine::setPosition(VoiceHandle handle, const glm::vec3 &position, const glm::vec3 &velocity) {
    PND_ASSERT(onMainThread(), "AUDIO: main thread only");
    Impl::Voice *voice = m_impl->resolve(handle);
    if (voice == nullptr) { return; }
    ma_sound_set_position(&voice->sound, position.x, position.y, position.z);
    ma_sound_set_velocity(&voice->sound, velocity.x, velocity.y, velocity.z);
}

void AudioEngine::seek(VoiceHandle handle, float seconds) {
    PND_ASSERT(onMainThread(), "AUDIO: main thread only");
    if (Impl::Voice *voice = m_impl->resolve(handle)) { ma_sound_seek_to_second(&voice->sound, std::max(seconds, 0.f)); }
}

bool AudioEngine::isValid(VoiceHandle handle) const {
    return m_impl->resolve(handle) != nullptr;
}

bool AudioEngine::isPaused(VoiceHandle handle) const {
    const Impl::Voice *voice = m_impl->resolve(handle);
    return voice != nullptr && voice->paused;
}

float AudioEngine::cursorSeconds(VoiceHandle handle) const {
    const Impl::Voice *voice = m_impl->resolve(handle);
    if (voice == nullptr) { return 0.f; }
    float cursor = 0.f;
    ma_sound_get_cursor_in_seconds(&voice->sound, &cursor);
    return cursor;
}

float AudioEngine::lengthSeconds(VoiceHandle handle) const {
    const Impl::Voice *voice = m_impl->resolve(handle);
    if (voice == nullptr) { return 0.f; }
    float length = 0.f;
    ma_sound_get_length_in_seconds(&voice->sound, &length);
    return length;
}

void AudioEngine::stopAll(float fadeSeconds) {
    PND_ASSERT(onMainThread(), "AUDIO: main thread only");
    for (Impl::Voice &voice : m_impl->voices) {
        if (voice.used) { stop(VoiceHandle{static_cast<uint32_t>(&voice - m_impl->voices.data()), voice.generation}, fadeSeconds); }
    }
}

void AudioEngine::setListener(const glm::vec3 &position, const glm::vec3 &forward, const glm::vec3 &up) {
    PND_ASSERT(onMainThread(), "AUDIO: main thread only");
    if (!m_impl->ready) { return; }
    ma_engine_listener_set_position(&m_impl->engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&m_impl->engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&m_impl->engine, 0, up.x, up.y, up.z);
}

bool AudioEngine::setBusVolume(std::string_view bus, float volume) {
    PND_ASSERT(onMainThread(), "AUDIO: main thread only");
    const int index = m_impl->busIndex(bus);
    if (index < 0) { return false; }
    m_impl->busVolumes[static_cast<size_t>(index)] = std::max(volume, 0.f);
    if (!m_impl->ready) { return true; }
    if (index == 0) {
        m_impl->applyMaster();
    } else {
        ma_sound_group_set_volume(&m_impl->groups[static_cast<size_t>(index)], m_impl->busVolumes[static_cast<size_t>(index)]);
    }
    return true;
}

float AudioEngine::busVolume(std::string_view bus) const {
    const int index = m_impl->busIndex(bus);
    return index < 0 ? 0.f : m_impl->busVolumes[static_cast<size_t>(index)];
}

void AudioEngine::setMuted(bool muted) {
    PND_ASSERT(onMainThread(), "AUDIO: main thread only");
    if (m_impl->muted == muted) { return; }
    m_impl->muted = muted;
    if (m_impl->ready) { m_impl->applyMaster(); }
}

bool AudioEngine::isMuted() const {
    return m_impl->muted;
}

AudioEngineStats AudioEngine::stats() const {
    AudioEngineStats stats;
    stats.voiceCapacity = VOICE_CAPACITY;
    stats.cachedClips = static_cast<uint32_t>(m_impl->cache.size());
    for (const Impl::Voice &voice : m_impl->voices) {
        if (voice.used) { stats.activeVoices++; }
    }
    if (m_impl->ready) {
        stats.sampleRate = ma_engine_get_sample_rate(&m_impl->engine);
        stats.channels = ma_engine_get_channels(&m_impl->engine);
        stats.deviceActive = m_impl->deviceActive;
    }
    return stats;
}

} // namespace Panda::Audio
