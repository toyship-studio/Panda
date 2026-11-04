# Audio: the engine part

`PandaAudio`: the contributions of the module to the engine registries.

- `AudioClipAsset` and the asset kind `audio` (`Panda/Audio/AudioClipAsset.hpp`): the encoded
  bytes of the file as a shared immutable buffer plus what its header declares (channels, rate,
  length). `load` probes the file with the decoder, `save` writes the bytes back.
- `AudioEngine` (`Panda/Audio/AudioEngine.hpp`): the mixer of the application, started and stopped
  by the `audio` application system (`Extensions::appSystems`) and ticked every frame. Voices
  (`VoiceHandle`, `VoiceParams`), the buses, the listener, the mute of the editor, the statistics.
  Main-thread only. The only sources that include miniaudio are `AudioEngine.cpp` and the clip
  probe: the public headers carry no type of the backend.
- `AudioSourceComponent` and `AudioListenerComponent` (`Panda/Audio/AudioComponents.hpp`, the
  DTOs in `AudioDtos.hpp`), the `audio` world system (`AudioSystem.cpp`: the voices of a running
  world in `World::context<AudioWorldState>`, the listener) and `Audio::Sources`
  (`Panda/Audio/AudioSources.hpp`), the entry the script host and the editor drive the sources
  through.
- The `audio` script table (`Panda/Audio/AudioScriptApi.hpp`, `AudioScriptHost.cpp`).
