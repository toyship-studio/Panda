#pragma once

namespace TerrainEditor {

// Icons of the editor part: PNGs in Modules/Terrain/editor/resources/terrain-icons (the folder gets into
// the editor resources and is registered in EditorExtensions::iconFolders, icon name = file name).
namespace Ic {
    inline constexpr const char *Terrain = "terrain"; // entity with a terrain (mask, tint)
    // White alpha masks, 128px for the 30px list rows and 64px Add Layer cards at 2x.
    // makeIcon supplies the current theme tint.
    inline constexpr const char *FloraTuft = "flora-tuft";
    inline constexpr const char *FloraGrassMesh = "flora-grass-mesh";
    inline constexpr const char *FloraBush = "flora-bush";
    inline constexpr const char *FloraTree = "flora-tree";
    inline constexpr const char *FloraRock = "flora-rock";
} // namespace Ic

} // namespace TerrainEditor
