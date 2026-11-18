# Audio: the SDK part

The scripting API of the module, compiled into `PandaSDK` and shipped as `include/Bamboo/…` of
the SDK package; it talks to the engine through the `audio` table (`Panda/Audio/AudioScriptApi.hpp`)
and does nothing in a game without the module.

- `Bamboo/AudioAPI.hpp` — the sounds a script plays itself and the mixer:
  - `AudioClipHandle` — an audio clip of the project as a script field (one, or a `std::vector`
    of them: a playlist, the variants of a footstep), chosen in the inspector; the cook of a game
    packs the clips its scripts are given;
  - `AudioAPI::play(clip, AudioPlayback)` — plays the clip (the bus, the volume, the pitch, a
    loop, a fade in, the second to start at; for a sound of a place its position and distances)
    and gives the `AudioVoice`; `stop`, `isPlaying`, `setPaused`, `setVolume`, `setPitch`,
    `setPosition`, `getTime` / `setTime` drive the voice, `getLength(clip)` reads the clip — how
    much of a piece of music is left, to bring the next one in over its end;
  - `AudioAPI::setBusVolume` / `getBusVolume` for `master`, `music`, `sfx`, `ui`, and `stopAll`.
- `Bamboo/Components/AudioSourceComponentAPI.hpp` — the audio source of an entity, for a sound
  that is a thing of the world (it stands somewhere, moves with its entity, is tuned in the
  inspector): `play`, `stop`, `setPaused`, `isPlaying`, `playOneShot` / `playOneShotAt` (a
  detached copy of the sound for overlapping shots and clicks), `setVolume` / `getVolume`,
  `setPitch` / `getPitch`, `getTime` / `setTime` and `getLength`. The clip and the spatial
  settings are the component's; a script reaches the source through its entity (an
  `EntityHandle` field).
