# Static resources and shaders

Static resources are the assets a module creates in code and keeps for the whole process: its
shaders, the materials that carry their field templates, procedural meshes, generated textures,
vertex layouts. They are created by a `ResourceProvider` after the core's static resources and
released before them.

## The provider

```cpp
// engine/src/Panda/Particles/ParticlesResources.cpp
namespace Panda::Particles::Resources {

AssetRef<ShaderAsset> shader;
AssetRef<MaterialAsset> material;
std::unique_ptr<RuntimeMesh> quadMesh;
Miren::VertexLayoutHandle instanceLayout{MIREN_INVALID_HANDLE};

namespace {
    const AssetId SHADER_ID = 0xE0000100u;
    const AssetId MATERIAL_ID = 0xE0000101u;

    void init(AssetHandler &handler) {
        shader = handler.createAsset<ShaderAsset>(SHADER_ID, "particles-shaders/particles.vert", "particles-shaders/particles.frag");
        shader->setSystemInputs(false, false); // unlit: no scene lights, no shadow map
        MaterialData data;
        data.inputs = {
            MaterialField("baseColorTexture", MaterialFieldType::TEXTURE, StaticResources::whiteTexture.asBaseAsset(), true),
            MaterialField("alphaMode", MaterialFieldType::UINT, 2u),   // blend
            MaterialField("doubleSided", MaterialFieldType::UINT, 1u),
        };
        material = handler.createAsset<MaterialAsset>(MATERIAL_ID, data, shader);
        quadMesh = std::make_unique<RuntimeMesh>();
        quadMesh->update(makeQuad());
        Miren::VertexBufferLayoutData instanceData;
        instanceData.pushVec4(); instanceData.pushVec4(); instanceData.pushVec4(); instanceData.pushVec4(); // mat4
        instanceData.pushVec4(); // color
        instanceLayout = Miren::createVertexLayout(instanceData);
    }

    void release() {
        shader.reset();
        material.reset();
        quadMesh.reset();
        if (instanceLayout.isValid()) { Miren::deleteVertexLayout(instanceLayout); }
        instanceLayout = Miren::VertexLayoutHandle{MIREN_INVALID_HANDLE};
    }
}

ResourceProvider provider() {
    ResourceProvider provider;
    provider.name = "particles";
    provider.init = init;
    provider.release = release;
    return provider;
}

} // namespace Panda::Particles::Resources
```

- `init` runs inside `StaticResources::initStaticResources`, after the core's assets and the
  core shader templates: the placeholder textures (`StaticResources::whiteTexture`,
  `blackTexture`, `flatNormalTexture`) and the general vertex layout
  (`StaticResources::generalVertexLayout`) exist. A provider registered while the static
  resources are already alive (a module loaded on project open) is initialized at once.
- `release` runs in `deinitStaticResources`, before the core releases its own; it resets
  every `AssetRef` and deletes the Miren handles the module created.
- The ids are persisted: a project material stores the id of its shader in the `.pmeta`
  sidecar (`meta.shader`), so a static shader id never changes. A module takes its ids from
  the `0xE0000000` range (the particles start at `0xE0000100`; take the next free hundred);
  the terrain keeps historical slots of the core sequence (23–28, 38–40, 53) that the core
  reserves and skips. Never reuse a slot of another module.

## Shaders

Module shaders live in a resource folder of the module, `engine/resources/<module>-shaders/*.hlsl`,
named `<name>.vert.hlsl` and `<name>.frag.hlsl`. The common baker compiles them with the
engine's (`slangc` to SPIR-V on macOS and Linux, the source on D3D11) into
`<module>-shaders/<name>.vert` and `.frag` of the resources, and the module loads them by that
path, like the core loads its own from `default-shaders/`. The shared includes of the core
(`Panda/resources/default-shaders/include/*.hlsli`: lighting, shadows, the UBO blocks generated
from `ShaderInterface`) are available to module shaders. Any other resource folder of the module
(`engine/resources/<module>-textures`) is shipped as it is; the folder name is the module's namespace.

`ShaderAsset::setSystemInputs(sceneLights, shadowMap, occlusionVolume, environmentMap)` tells
the renderer which system inputs the shader declares; built-in shaders have no SPIR-V
reflection, so the flags are the contract (a lit terrain: `true, true, true`; a depth pass:
`false, false`).

## Materials and templates

A material is a `MaterialData` of named fields (`MaterialField {name, type, value, runtime}`):
`FLOAT`, `UINT`, `VEC4`, `TEXTURE`, `COLOR`. A `runtime` field is fed by the system every
frame (`material->setRuntimeField("baseColorTexture", texture)`) and is neither written to
`.mat` nor shown in the inspector. Service vectors are `VEC4`, not `COLOR`: a color goes
through sRGB on packing.

A material shader the user may pick for project materials is a `ShaderTemplate`:

```cpp
Extensions &extensions = GameContext::getExtensions();
extensions.shaderTemplates.add({shader2D.getId(), "Terrain", material2D});
```

The template material carries the field set; the editor's shader picker lists the templates
in registration order, and a `.mat` on this shader is reconciled against the template's fields
on load and on a shader change. The registry is cleared with the static resources, so the
template is added from `init`.

## A custom vertex layout

A mesh with its own vertex format (the thin `TerrainVertex`) cannot go through the general
depth and id programs. The module registers the programs for its layout, from `init`:

```cpp
extensions.meshLayouts.add({vertexLayout, shadowDepthShader, idsMaterial});
```

The renderer takes them for the shadow depth passes and the id pass when it draws a
`RuntimeMesh` of that layout through `Renderer3D::submit(id, model, mesh, material)`.

## The renderer

What a system submits in the scene pass (`WorldSystem::submit`, [World systems](world-systems.md)):

- `submit(id, model, const RuntimeMesh &mesh, material)` — a mesh the module owns (chunks,
  procedural geometry); `id` is the entity id for picking and the selection outline;
- `submit(id, model, AssetRef<RuntimeMeshAsset>, material)` and the imported-mesh overload —
  registry assets;
- `submitInstanced(model, mesh, material, instanceBuffer, instanceLayout, count)` — one draw
  for many instances; the instance stream is a vertex buffer in `instanceLayout` (for plain
  transforms `StaticResources::instanceMat4Layout`, the columns of a `glm::mat4`), the shader
  declares the instance attributes after the vertex ones. Instances are drawn in `DEFAULT`
  and `SHADOW_DEPTH` and are neither picked nor outlined.

Miren resources a system creates per world (instance buffers) live in the world context and
are deleted in its destructor; the editor has two worlds and the smoke test several.
