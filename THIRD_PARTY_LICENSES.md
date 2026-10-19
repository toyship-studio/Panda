# Third-party components

Panda is licensed under the Panda Engine License (`LICENSE.txt`, `NOTICE`). The repository vendors
the libraries below under their own licenses; the license text of each lives next to its sources.

| Component | Where | License | License text |
|---|---|---|---|
| GLFW | `Vendor/glfw` | zlib/libpng | `Vendor/glfw/LICENSE.md` |
| glad 0.1.34 (generated OpenGL loader) | `Vendor/glad` | generated code is public domain / WTFPL / CC0 at the user's choice, per the glad project; `KHR/khrplatform.h` carries the Khronos notice in the file | the notice at the top of `Vendor/glad/include/KHR/khrplatform.h` |
| GLM | `Vendor/glm` | MIT (or The Happy Bunny License) | `Vendor/glm/copying.txt` |
| EnTT | `Vendor/entt` | MIT | `Vendor/entt/LICENSE` |
| Yoga | `Vendor/yoga` | MIT | `Vendor/yoga/LICENSE` |
| Box2D | `Vendor/box2d` | MIT | `Vendor/box2d/LICENSE` |
| Box3D | `Vendor/box3d` | MIT | `Vendor/box3d/LICENSE` |
| cgltf | `Vendor/cgltf` | MIT | `Vendor/cgltf/LICENSE` |
| stb_image, stb_truetype | `Vendor/stb_image`, `Vendor/stb_truetype` | MIT or public domain, at the user's choice | the end of each header |
| SPIRV-Cross | `Vendor/spirv-cross` | Apache-2.0 | `Vendor/spirv-cross/LICENSE` |
| dylib | `Vendor/dylib` | MIT | `Vendor/dylib/LICENSE` |
| RapidJSON | `Rain/Vendor/rapidjson` | MIT (bundled msinttypes and json checker have their own notices in the same file) | `Rain/Vendor/rapidjson/license.txt` |
| yaml-cpp | `Rain/Vendor/yaml-cpp` | MIT | `Rain/Vendor/yaml-cpp/LICENSE` |
| Native File Dialog | `Vendor/nfd` | zlib | `Vendor/nfd/LICENSE` |
| ImGuizmo, the origin of the viewport gizmo code | `PandaEditor/src/Viewport/Gizmo/Gizmo.cpp` | MIT | the header of the file |
| Inter | `Panda/resources/default-fonts/Inter` | SIL Open Font License 1.1 | `Panda/resources/default-fonts/Inter/LICENSE.txt` |
| JetBrains Mono | `Panda/resources/default-fonts/JetBrainsMono` | SIL Open Font License 1.1 | `Panda/resources/default-fonts/JetBrainsMono/LICENSE.txt` |
| Slang, the shader compiler bundled with the editor and the SDK | `Vendor/tools/slang`, downloaded by `scripts/fetch_shader_tools` | Apache-2.0 WITH LLVM-exception | `LICENSE` in the downloaded prefix; the text is also embedded in the generator |
| CMake, bundled with the editor and the SDK | copied from the build machine at build time | BSD-3-Clause | embedded in the generator |

The binaries carry every text above in one file, after the engine's own notice and license:
`scripts/third_party_notices.py` assembles `THIRD_PARTY_NOTICES.txt` (the editor, the SDK package
and every exported game). Regenerate it when a vendored library or the license changes; the rows
above are the index.
