#pragma once

namespace Editor {

class EditorExtensions;

// The asset editors of the core kinds (texture import settings, material, postprocessing chain,
// mesh material slots), registered in EditorExtensions::assetKinds like the kinds of the modules
// (context/plugins.md, decision 2): an asset tab builds its form from the registry only.
void registerCoreAssetKinds(EditorExtensions &extensions);

} // namespace Editor
