#pragma once

namespace Editor {
class EditorExtensions;
}

namespace PhysicsEditor {

// Contributions of the editor part of the Physics module to the editor registries
// (PandaEditorKit, context/plugins.md): the inspector sections of the bodies, colliders and
// joints, the collider outlines of the viewport (a viewport overlay) and the PHYSICS section of
// the editor settings ("physics" of the user's settings.json: Show Colliders). Entry point —
// PhysicsEditorEntry.cpp (PANDA_PACKAGE_EDITOR_ENTRY).
void registerExtensions(Editor::EditorExtensions &extensions);

} // namespace PhysicsEditor
