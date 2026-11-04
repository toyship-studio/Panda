# Audio: the editor part

`PandaAudioEditor` (namespace `AudioEditor`, on PandaEditorKit):

- the look and the tab of the `audio` asset kind: `AudioClipEditor` — the format, channels, sample
  rate, duration and size of the clip, how it will be decoded, and a transport (Play / Pause /
  Stop, the position, loop) that plays the clip through the mixer on the `ui` bus;
- the AUDIO section of the editor settings (`AudioEditorSettings`, the key `audio` of the user's
  `settings.json`): Mute, applied to the mixer every frame by the `audio.editor` application system;
- the AUDIO statistics: voices, cached clips, the sample rate of the device.
