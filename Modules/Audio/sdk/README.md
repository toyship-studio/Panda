# Audio: the SDK part

The scripting API of the module, compiled into `PandaSDK` and shipped as `include/Bamboo/…` of
the SDK package; it talks to the engine through the `audio` table (`Panda/Audio/AudioScriptApi.hpp`)
and does nothing in a game without the module.

- `Bamboo/AudioAPI.hpp` — `AudioAPI::setBusVolume` / `getBusVolume` for `master`, `music`, `sfx`,
  `ui`, and `stopAll`.
- `Bamboo/Components/AudioSourceComponentAPI.hpp` — the audio source of an entity: `play`,
  `stop`, `setPaused`, `isPlaying`, `playOneShot` / `playOneShotAt` (a detached copy of the sound
  for overlapping shots and clicks), `setVolume` / `getVolume`, `setPitch` / `getPitch`. The clip
  and the spatial settings are the component's, set in the inspector; a script reaches a sound
  through the entity that carries it (an `EntityHandle` field).
