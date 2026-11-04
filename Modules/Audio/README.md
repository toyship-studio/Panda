# Audio

Sound for Panda projects: audio clips as assets, a mixer with buses, audio sources and a listener
in the world, playback from scripts. An engine module (`dev.pandaengine.audio`); the engine core
knows nothing about sound.

- **Clips** are the audio files of the project: `.wav`, `.ogg` (Vorbis), `.mp3`, `.flac`. They
  appear in the Content Browser like any asset; the tab of a clip shows what the file declares and
  plays it. The export copies the files as they are; nothing is converted.
- **The mixer** runs for the whole application on [miniaudio](https://miniaud.io): the playback
  device of the system (the null backend when there is none), four buses — `master`, `music`,
  `sfx`, `ui` — and a pool of 64 voices. A clip shorter than 10 seconds is decoded once and shared
  by its voices, a longer one is decoded while it plays.
- **In the world**: an entity with an **Audio Source** plays a clip — on a bus, at a volume and
  pitch, looped or once, at start or when a script says so; **spatial** sources are attenuated and
  panned by their distance and direction from the **Audio Listener** (an entity with that
  component, or the main camera). Scripts drive sources through
  `Bamboo::AudioSourceComponentAPI` (play, stop, pause, one-shots for overlapping sounds, volume,
  pitch) and the buses through `Bamboo::AudioAPI`.
- **In the editor**: the clip tab (Play / Pause / Stop, the position, loop), the inspector sections
  of the source and the listener (with Play / Stop: the live voice in Play, a preview otherwise),
  the AUDIO section of the editor settings (Mute: everything the editor plays, previews and Play
  alike) and the AUDIO statistics (voices, cached clips, the sample rate of the device).

The parts: `engine/` (the asset kind, the mixer, the components and the world system, the script
table), `editor/` (the clip tab, the inspector sections, the settings and statistics), `sdk/`
(the Bamboo API). The design is `context/audio.md` of the engine repository.

## Platforms

macOS, Windows, Linux (ALSA / PulseAudio / JACK are loaded at run time), iOS. The Apple builds link
CoreFoundation, CoreAudio and AudioToolbox (and AVFoundation on iOS); the game export gets them
from the SDK package.
