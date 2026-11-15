#pragma once

namespace Editor {

class EditorExtensions;

// The sections of the components of the engine core — the camera, the lights, the sprite, the
// meshes, the sky — in the registry every component section comes from
// (EditorExtensions::componentInspectors): the core adds its own the way a module does.
void registerCoreComponentInspectors(EditorExtensions &extensions);

} // namespace Editor
