#pragma once

namespace Editor {
class EditorExtensions;
}

namespace AudioEditor {

// The inspector sections of the Audio Source and Audio Listener components
// (AudioComponentSections.cpp): the fields of the source, a Play / Stop that plays the live voice
// of a running world or a preview through the mixer otherwise.
void registerComponentSections(Editor::EditorExtensions &extensions);

} // namespace AudioEditor
