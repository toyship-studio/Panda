#pragma once

namespace Editor {
class EditorExtensions;
}

namespace ParticlesEditor {

// Editor part of the Particles module: the "Particle Emitter" inspector section (the
// componentInspectors registry) — all the component's fields, edits as a single undo command.
void registerExtensions(Editor::EditorExtensions &extensions);

} // namespace ParticlesEditor
