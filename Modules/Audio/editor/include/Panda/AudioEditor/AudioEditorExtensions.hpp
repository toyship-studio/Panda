#pragma once

namespace Editor {
class EditorExtensions;
}

namespace AudioEditor {

// Contributions of the editor part of the Audio module (PandaEditorKit registries): the look and
// the tab of the "audio" asset kind (the clip tab with playback), the inspector sections of the
// audio source and listener components, the AUDIO section of the editor settings (mute) and the
// AUDIO statistics.
void registerExtensions(Editor::EditorExtensions &extensions);

} // namespace AudioEditor
