# Roadmap

Large directions that are still ahead. Finished work is one line with a pointer to its contract.

## Done

- **PBR and materials**: built-in Unlit / Lit / PBR, creation from the Content Browser, dirty
  state and Cmd+S.
- **Material preview**: `ThumbnailsManager`, live preview, asynchronous thumbnails.
- **Post-processing and HDR**: a `.postfx` chain on the camera, linear RGBA16F, implicit tonemap,
  built-in bloom.
- **Joints 2D/3D**: Revolute, Distance, Spherical and Wheel components with test scenes; the
  wheel joint is driven from scripts (`context/physics.md`). Not started: a ragdoll scene, a motor
  API for the revolute joint, helper drawing.
- **3D physics**: box3d, colliders, character mover, events, ray cast and body dynamics for
  scripts (`context/physics.md`).
- **Vehicles**: a car on wheel joints, a car on rays and an obstacle track in PhysicsTest; the
  fixed physics step with `fixedUpdate` and interpolated poses (`context/physics.md`).
- **Metal backend**: the default on desktop Apple platforms; D3D11 and OpenGL/GLES exist.
- **Terrain 2D/3D**, including flora (`context/terrain.md`).
- **Shadows**: CSM and the Occlusion Volume (`context/lighting.md`).
- **glTF material import**: materials are created on import (PBR default, shared `<name>.mat`,
  empty slots merged); textures need no manual import.
- **Export Game / Runtime**: export without engine sources (PandaSDK), cooked `.Panda`,
  PandaRuntime; on macOS the SDK ships inside `PandaEditor.app`.
- **Shader toolchain**: slangc plus vendored SPIRV-Cross, no Vulkan SDK
  (`context/rendering.md`).
- **Smoke tests**: `Miren/Smoke` and `Panda/Smoke` (`scripts/*_run_smoke.*`), Metal and GL.
- **Editor scene UX core**: editor tabs, asset well, asset picker, the document model, rollback
  of Play mutations.
- **Safer script handles**: `resolveScriptEntity` guards across the whole ABI.
- **Modules and plugins**: engine and editor registries, terrain and particles as modules, theme
  plugins (`context/plugins.md`).

## Ahead (order is open to discussion)

0. **Static meshes in the Occlusion Volume**: on demand; see "Open items" in
   `context/lighting.md`. Rejected there: clouds and a sun mask, far shadows beyond the CSM
   distance, CSM changes.
1. **Public release and infrastructure**: CI, signed builds, auto-update, the documentation site,
   the portal and the plugin registry (`context/infrastructure.md`). The engine-side preparation
   (frame phases, core systems and physics through the registries, the settings scopes, the
   document model, the module author guide) is done. Later a Bamboo API reference generated from
   the SDK headers, on the documentation site and as help inside the editor.
2. **Richer scenes and editor UI polish**: models through `scripts/fbx_to_gltf.sh`, vegetation
   assets.
3. **Sound**: asset import and cooking, runtime playback, spatial audio, SDK API, editor preview.
4. **Demo games**: bring workflows to production quality regularly (quality, not quantity).
5. **Multiple worlds / scene management**: a scene list and a start scene in project settings.
6. **Prefabs**: after world, assets and script fields stabilize; a prefab is an asset with
   overrides.
7. **Input actions**: a mapping layer over raw input (mobile, gamepad, remapping).
8. **Cooking and packaging**: finish "the runtime never reads authoring formats". Deferred: a
   cached snapshot of the asset registry in `.Panda/` (path → id, mtime), needed only at tens of
   thousands of assets; a cache is never the truth and never goes into git.
9. **Skeletal animation**: import, runtime pose, clips, skinning, preview.
10. **UI and text**: clipboard and IME basics, accessibility basics, richer controls. A view
    hierarchy debugger for game UI in the editor (Play → Pause → snapshot of all UI windows →
    tree, properties and a frozen preview) is planned but not started.
11. **Editor scene UX, next steps**: better hierarchy, prefab workflow, scene operations.
    A command palette as in Zed and VS Code: editor commands and theme selection go there instead
    of growing the menus. Copy
    and paste of components in the inspector — also the way to carry values tuned in Play over
    to the world being edited (Play edits are discarded on Stop, and Save in Play does not save
    them). Undecided: auto save on a timeout (the doubt is terrain, whose `.terrain` file is
    rewritten as a whole); if done, a timeout in Settings plus a debounce.
12. **Editor structure**: `EditorLayer` is wide rather than long (it implements several output
    protocols at once); splitting it by axis of change is analyzed but not done.
13. **Vulkan**: not implemented; on Apple use MoltenVK to check performance; do not block
    export or mobile on it (the GLES path exists).
14. **Android**: do not name mobile logic iOS-specific; the GLES path suits Android too.
15. **Swift scripting**: possible with a flat C ABI; not a priority, do not complicate ahead.
16. **Dev tools** (independent of each other): debug drawing (the engine has none: command
    buffers rendered under a dev flag, wanted in the script ABI), compound undo
    (`BeginCompound` / `EndCompound` in `CommandHistory`: multi-selection and "add component plus
    defaults" in one Cmd+Z), a file watcher over `AssetHandlerEditor::reload`, a dev build of the
    game separate from `PND_EDITOR`.
17. **Editor localization**: `tr()` across the editor plus string plugins, once the UI settles.
18. **Vegetation as a module**: the flora impostors and the vegetation material templates out of
    the core into `dev.pandaengine.vegetation`, which Terrain and the authoring tools depend on
    (`context/plugins.md`, open items).
