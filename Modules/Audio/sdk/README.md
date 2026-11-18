# Audio: the SDK part

The scripting API of the module, compiled into `PandaSDK` and shipped as `include/Bamboo/…` of
the SDK package; it talks to the engine through the `audio` table (`Panda/Audio/AudioScriptApi.hpp`)
and does nothing in a game without the module.

- `Bamboo/AudioAPI.hpp` — `AudioAPI::setBusVolume` / `getBusVolume` for `master`, `music`, `sfx`,
  `ui`, and `stopAll`.
- `Bamboo/Components/AudioSourceComponentAPI.hpp` — the audio source of an entity: `play`,
  `stop`, `setPaused`, `isPlaying`, `playOneShot` / `playOneShotAt` (a detached copy of the sound
  for overlapping shots and clicks), `setVolume` / `getVolume`, `setPitch` / `getPitch`,
  `getTime` / `setTime` (where the source plays, in seconds of its clip) and `getLength` (of its
  clip): how much of a piece of music is left, to bring the next one in over its end. The clip
  and the spatial settings are the component's, set in the inspector; a script reaches a sound
  through the entity that carries it (an `EntityHandle` field).
