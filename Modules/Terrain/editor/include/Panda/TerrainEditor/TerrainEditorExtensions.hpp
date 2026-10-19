#pragma once

namespace Editor {
class EditorExtensions;
}

namespace TerrainEditor {

// Contributions of the editor part of the Terrain module to the editor registries (PandaEditorKit,
// context/plugins.md, stage 2): the Terrain 2D/3D inspector sections, the tab forms of .terrain
// assets, the viewport brush, the Settings sections (TERRAIN, FLORA), the "New Terrain" items,
// the flora statistics section, icons. Entry point — TerrainEditorEntry.cpp
// (PANDA_PACKAGE_EDITOR_ENTRY).
void registerExtensions(Editor::EditorExtensions &extensions);

} // namespace TerrainEditor
