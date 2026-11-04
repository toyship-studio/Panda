# Changelog

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
