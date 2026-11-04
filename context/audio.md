# Audio

Sound in the engine: audio clips as assets, the mixer of the application, audio sources and the
listener in the world, playback for scripts. Audio is the module `Modules/Audio`
(`dev.pandaengine.audio`, the static libraries `PandaAudio` and `PandaAudioEditor`); the engine
core knows nothing about sound (`context/plugins.md`). The backend is the vendored miniaudio
(`Vendor/miniaudio`, public domain / MIT‑0) with `stb_vorbis` for Vorbis.

## Decisions

1. **A module, not the core.** Sound has the shape of a module: its own asset kind, components, a
   world system, a script table, an editor part, a vendored dependency and platform frameworks. A
   project without sound links none of it. What the core needed from the module work is generic:
   the application systems registry (below) and the asset destructor on reload.
2. **miniaudio for the device, the decoders and the mixing engine; the policy is ours.** The
   device backends (Core Audio, WASAPI, ALSA / PulseAudio / JACK, AAudio, Web Audio), format
   conversion, the decoders (WAV, FLAC, MP3 built in, Vorbis through stb_vorbis), the resampler,
   the spatializer and the node graph come from one file. Voices, their stealing, the buses, the
   cache of clip data, the listener policy and the editor behaviour are the module's. Only
   `AudioEngine.cpp` and the clip probe include `miniaudio.h`: the public headers of the module
   carry no `ma_*` type, so the backend can be replaced inside the module (a lower layer of
   miniaudio with an own mixer, or an own `Fern::Audio`) without touching the contracts.
   Considered and set aside: an own device layer in Fern plus an own mixer (5–6 thousand lines of
   platform code before the first feature, the bugs of other people's hardware), SoLoud (less
   alive, no AAudio), OpenAL Soft (LGPL for games linked statically), FMOD / Wwise (proprietary).
3. **The mixer belongs to the application, voices to a world.** The editor has two worlds (the
   one being edited and the Play copy) and a game switches worlds; sounds must not die with a
   world object. `AudioEngine` is one per process, started and stopped by the `audio` application
   system; which voice belongs to which entity of which world is the state of the audio world
   system in `World::context<T>` (stage 2).
4. **Clip data is a shared immutable buffer.** The asset is one owner, the mixer's cache another:
   a reload of the asset replaces the buffer, a playing voice keeps the old one until it is done.
   The cache drops a buffer when no asset and no voice holds it.
5. **Fixed buses by string name** — `master`, `music`, `sfx`, `ui`. A string in the component and
   the API lets the set grow without a migration; a project-configurable set waits for the
   project-scope sections to reach the runtime (`project.bin` carries none today).
6. **Decoding policy by duration, not by an import setting.** A clip shorter than
   `DECODE_IN_MEMORY_SECONDS` (10 s) is decoded once into PCM and shared by its voices; a longer
   one is decoded while it plays (music). An import setting per clip needs typed settings on
   module asset kinds in the core (`AssetKind` has no codec; `AssetInfo` is a closed variant) —
   the generic extension comes when the heuristic is not enough.
7. **Main thread only; the audio clock is the device's.** Every `AudioEngine` call is asserted on
   the main thread. The audio thread is the device callback of miniaudio: no code of the engine
   runs on it (the end callbacks of sounds are not used; `at_end` is polled in the tick). The
   decoding of a clip at its first play is synchronous (milliseconds for a sound effect); the
   resource manager of miniaudio keeps its one job thread for what it loads itself.
   `Application::getTime()` is a float game clock and is never used for sound.

## Contract

### Clips (`Modules/Audio/engine/include/Panda/Audio/AudioClipAsset.hpp`)

- The asset kind `audio`: `AudioClipAsset`, the id `CLIP_TYPE = AssetKind::idOf("audio")`
  (persisted in `.pmeta` and the runtime registry; the kind name never changes), the folder
  `Audio`, the extensions `.wav .ogg .mp3 .flac` in both spellings of case. A file-based kind: the
  editor and the runtime read the same file, the export copies it as it is.
- `load` reads the bytes and probes the header with the decoder (channels, sample rate, frame
  count; the format name is the extension) — no decoding; an unreadable file is `nullptr` (the
  editor shows the asset as missing and the placeholder, an empty clip that plays nothing). `save`
  writes the bytes back (the document model of a file kind asks for a writer).
- `AudioClipAsset::bytes()` is `shared_ptr<const vector<uint8_t>>` (decision 4), `info()` is
  `AudioClipInfo {channels, sampleRate, frameCount, format}` with `durationSeconds()`.

### The mixer (`Panda/Audio/AudioEngine.hpp`)

- `AudioEngine::instance()` is valid between the `start` and `stop` of the `audio` application
  system; `update()` runs in its `tick`: finished and faded-out voices go back to the pool, the
  cache drops the clips nobody holds.
- The device: the default playback device of the system; when there is none (a CI runner, a
  headless machine) the null backend of miniaudio keeps the clock running so that voices finish
  and the API behaves (`AudioEngineStats::deviceActive` is false). The mixer runs at the native
  rate and channel count of the device. On Apple the frameworks are linked
  (`MA_NO_RUNTIME_LINKING`: the editor is signed with the hardened runtime).
- Voices: a pool of `VOICE_CAPACITY` = 64, `VoiceHandle {index, generation}` is weak — a stale
  handle makes every call a no-op. `play(clip, VoiceParams)` registers the clip's buffer with the
  resource manager (named by the buffer's identity), decodes it or not by decision 6, puts the
  voice on its bus and starts it (or leaves it paused with `startPaused`). A full pool steals the
  oldest non-looping voice; when every voice loops, `play` fails. `stop(handle, fade)` fades out
  (20 ms by default, against clicks) and releases; `setPaused`, `apply(params)` (the tunable
  part: volume, pitch, loop, the spatial fields), `setPosition(position, velocity)`, `seek`,
  `cursorSeconds`, `lengthSeconds`, `stopAll`.
- `VoiceParams`: bus, volume, pitch (> 0), loop, spatial, position, velocity, minDistance,
  maxDistance, rolloff, `AttenuationModel` (None 0, Inverse 1, Linear 2, Exponential 3 — persisted
  as numbers, never reordered), dopplerFactor, fadeInSeconds, startPaused. A non-spatial voice is
  mixed as it is (`MA_SOUND_FLAG_NO_SPATIALIZATION`).
- The listener (one): `setListener(position, forward, up)`.
- Buses: `setBusVolume(name, volume)`, `busVolume(name)` for the four names of `BUS_NAMES`; the
  master is the output volume of the engine, the others are sound groups mixing into it. An
  unknown bus name plays on `sfx`. `setMuted` silences the output without touching the master
  volume (the editor's Mute).
- `stats()`: active voices, capacity, cached clips, sample rate, channels, `deviceActive`.

### The application systems registry (core: `Panda/Extensions/AppSystems.hpp`)

`Extensions::appSystems` — `AppSystem {name, start(), tick(deltaTime), stop()}`: a service that
lives for the process and not for a world. `Application` drives the registry: live on construction
(the registered systems start, in order; a system registered later — the modules of a project in
the editor — starts at registration), `tick` in `Application::tick` after the layers updated and
before the completions of the scheduler run, `stop` in `~Application` after the layers and their
worlds are destroyed, in reverse order. Not called where the process is killed without a teardown
(iOS). The first consumers are the `audio` system of the engine part and the `audio.editor` system
of the editor part (the Mute of the editor settings applied every frame).

### The editor part

- `AssetKindUi {"audio", Ic::Audio, …}` and `AudioClipEditor`, the tab of a clip: the format,
  channels, sample rate, duration, size and the decoding policy of the clip; a transport (Play /
  Pause / Stop, a progress bar polled every frame, Loop) that plays the clip through the mixer on
  the `ui` bus, not spatial. The tab stops its voice when it closes. There is no document and no
  dirty state: a clip has nothing to edit.
- `AudioEditorSettings {mute}` — the key `audio` of the editor scope (the user's `settings.json`),
  the AUDIO section of the editor settings; Mute silences everything the editor plays, clip
  previews and Play alike.
- Statistics: the AUDIO section — Voices, Clips (cached), Sample Rate.

### Build and packaging

- `Vendor/miniaudio`: `include/miniaudio/miniaudio.h` (the include spelling is the same in the
  tree and in the SDK), one implementation unit (`miniaudio.cpp`; `miniaudio.mm` on Apple, the
  Core Audio backend is Objective-C, no ARC), `stb_vorbis.c` as a C object, the options as PUBLIC
  definitions (`MA_NO_ENCODING`; `MA_NO_RUNTIME_LINKING` on Apple) so that the module's units see
  the header with the options the implementation was built with. Apple links CoreFoundation,
  CoreAudio, AudioToolbox (iOS also AVFoundation); Linux `pthread m dl` (ALSA / PulseAudio / JACK
  are loaded at run time).
- The SDK package: miniaudio is in the lists of `PandaRuntime/CMakeLists.txt` (prebuilt per
  platform), `PandaRuntimeConfig.cmake.in` (`PandaSDK::miniaudio` with the same definitions and
  frameworks; linked by the Audio module only, not by every game) and the header copy of
  `PandaSDK/CMakeLists.txt`. The module's `CMakeLists.txt` links `miniaudio` in the tree and
  `PandaSDK::miniaudio` in an export, like Physics with box2d.
- Licenses: `THIRD_PARTY_LICENSES.md` rows for miniaudio and stb_vorbis, entries in
  `scripts/third_party_notices.py`.
- Core fix that came with the module: a reload or a removal of an asset runs its destructor
  (`F_DELETE` instead of `F_FREE` in `AssetHandlerEditor` and `AssetHandlerRuntime`): an asset
  owns more than its bytes (GPU textures, the buffers of a module kind).

### The world (`Panda/Audio/AudioComponents.hpp`, `AudioSources.hpp`, `AudioSystem.cpp`)

- `AudioSourceComponent` (the key `audioSourceComponent`, DTO `AudioDtos.hpp`): `clip`
  (`AssetRef<Asset>`, an `AudioClipAsset`), `bus`, `volume`, `pitch`, `loop`, `playOnStart`,
  `spatial`, `minDistance`, `maxDistance`, `attenuation` (persisted as the number of
  `AttenuationModel`), `dopplerFactor`. The component is the truth; its voice is runtime state.
- `AudioListenerComponent` (the key `audioListenerComponent`, a tag written as `true`): the entity
  spatial voices are heard from — position, forward (−Z), up (+Y), as a camera. Without one the
  listener is the main camera (`World::findMainCameraEntity`).
- The world system `audio`, `after = {"scripts"}` (it hears the camera the scripts moved in
  `lateUpdate`): `start` plays the sources with `playOnStart`; `stop` stops the voices and the
  one-shots of the world and erases the state; `entityRemoved` stops the entity's voice,
  `entityRestored` plays it again if `playOnStart`; `componentsChanged` stops the voice of a removed
  source and starts an added one with `playOnStart`; `postUpdate` sets the listener, and for every
  voice drops the entry of a finished one, re-applies the tunable fields of the component when
  they differ from the ones applied (an inspector edit, an undo, a script setter reach the voice
  without a hook), and pushes the world position and, with Doppler, the velocity from the position
  delta of the frame. State: `World::context<AudioWorldState>` — entity id → voice and the
  applied component; the one-shots the world owns. Handles are weak: the destructor of the state
  touches nothing.
- `Audio::Sources` — the sources of a running world as the script host and the editor drive them:
  `play` (from the component; a playing one starts over), `stop(fade)`, `setPaused`, `isPlaying`,
  `playOneShot(position, volumeScale, pitchScale)` (a detached voice with the source's settings
  that does not follow the entity and ends by itself). Outside a running world every call is a
  no-op.

### The script table (`Panda/Audio/AudioScriptApi.hpp`, SDK part `Modules/Audio/sdk`)

- The contribution table `"audio"` v1, `AudioHostApi`: `source_Play / Stop / SetPaused /
  IsPlaying / PlayOneShot(position3 | null, volumeScale, pitchScale) / SetVolume / GetVolume /
  SetPitch / GetPitch` on an entity handle (the volume and the pitch are written into the
  component, the system applies them at the end of the frame), `bus_SetVolume / GetVolume`,
  `stopAll(fade)`. An entity without a source, or no running world, returns 0 and zeros.
- The Bamboo API: `Bamboo::AudioAPI` (`setBusVolume`, `getBusVolume`, `stopAll`) and
  `Bamboo::AudioSourceComponentAPI` (`play`, `stop`, `setPaused`, `isPlaying`, `playOneShot`,
  `playOneShotAt`, `setVolume` / `getVolume`, `setPitch` / `getPitch`). A script reaches a sound
  through the entity that carries the Audio Source (an `EntityHandle` field): there is no clip
  field type for scripts until the core has a generic asset field by kind name
  (`ScriptFieldType::ASSET`, an ABI bump).
- The inspector: the Audio Source section (the clip well of kind `audio`, the bus, volume, pitch,
  loop, play on start, spatial and its distances, attenuation, Doppler; Play / Stop — in a running
  world the live voice of the entity through `Sources`, otherwise a preview through the mixer,
  not spatial) and the Audio Listener section (a hint).

## Ahead

Music that survives a world switch (voices of the application level plus a heartbeat of the
running world in the tick: "no world ticked this frame" tells a Stop from a switch and pauses the
voices when Play pauses); import settings per clip (typed settings on module asset kinds in the
core); a clip field type for scripts (the generic `ScriptFieldType::ASSET` by kind name); a
waveform in the clip tab; suspend / resume and the audio session category on iOS (Fern lifecycle
events → `AppSystem::suspend/resume`); a configurable bus set and voice limit (project-scope
sections cooked into `project.bin`); effects (a low-pass for occlusion, reverb zones as an own
node of the graph); Android (AAudio is in miniaudio).
