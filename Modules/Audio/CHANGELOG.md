# Changelog

## 0.3.0

- A script holds clips and plays them without an entity per sound: `Bamboo::AudioClipHandle` as a
  script field (one, or a `std::vector` of them), `AudioAPI::play(clip, AudioPlayback)` and the
  `AudioVoice` it gives — `stop`, `isPlaying`, `setPaused`, `setVolume`, `setPitch`,
  `setPosition`, `getTime` / `setTime` —, `AudioAPI::getLength(clip)` (the `audio` table is v3;
  scripts are built again). The Audio Source stays for a sound that is a thing of the world.

## 0.2.0

- Scripts read where a source plays and how long its clip is:
  `AudioSourceComponentAPI::getTime` / `setTime` / `getLength` (the `audio` table is v2; scripts
  are built again). With them a script brings the next piece of music in over the end of the
  playing one.

## 0.1.0

- Audio clips as a file-based asset kind (`.wav`, `.ogg`, `.mp3`, `.flac`), read by the editor and
  the runtime alike, copied as they are by the export.
- The mixer of the application on miniaudio: the playback device or the null backend, the buses
  `master` / `music` / `sfx` / `ui`, 64 voices, decode-once for short clips and decode-while-playing
  for long ones, a listener for spatial voices.
- The Audio Source and Audio Listener components, the `audio` world system (voices follow their
  components and entities every frame; the listener follows its entity or the main camera), the
  `audio` script table with `Bamboo::AudioAPI` and `Bamboo::AudioSourceComponentAPI`.
- Editor: the clip tab with playback, the inspector sections of the components, the AUDIO editor
  settings (Mute), the AUDIO statistics.
