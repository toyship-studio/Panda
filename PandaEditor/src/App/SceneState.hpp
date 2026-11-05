#pragma once

namespace Panda {

// The state of the editor's world: EDIT — the editing world, nothing runs; PLAY — the playing
// copy runs (physics, scripts). Which camera the frame is seen through is the business of the
// viewports (Viewport/EditorViewports.hpp): the Scene tab looks with the editor camera, the Game
// tab with the main camera of the world.
enum class SceneState { EDIT = 0, PLAY = 1 };

} // namespace Panda
