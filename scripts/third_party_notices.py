#!/usr/bin/env python3
"""Assembles the notices that ship with the binaries: the engine's own notice and license, then
the third-party components.

Writes THIRD_PARTY_NOTICES.txt at the repository root: the editor, the SDK package and every
exported game carry it, which is how a game meets the notice conditions of the engine license. The
texts come from NOTICE, LICENSE.txt and the license files next to the vendored sources; the index
with locations is THIRD_PARTY_LICENSES.md. An application that builds the engine as a subproject
imports this module and renders its own file with render(header). Run from anywhere after a
vendored library or the license changes:

    python3 scripts/third_party_notices.py
"""

import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
RULE = "-" * 80


def file_text(relative):
    return (ROOT / relative).read_text(encoding="utf-8").strip()


def comment_block(relative, marker):
    """The /* ... */ block of a C header that contains the marker, without the comment decoration."""
    lines = (ROOT / relative).read_text(encoding="utf-8").splitlines()
    start = next(i for i, line in enumerate(lines) if marker in line)
    while not lines[start].lstrip().startswith("/*"):
        start -= 1
    end = next(i for i in range(start, len(lines)) if "*/" in lines[i])
    block = []
    for line in lines[start + 1:end]:
        block.append(re.sub(r"^\s*\*\*?\s?", "", line).rstrip())
    return "\n".join(block).strip()


def line_comment_header(relative):
    """The leading // comment lines of a source file (its license header), without the slashes."""
    lines = (ROOT / relative).read_text(encoding="utf-8").splitlines()
    block = []
    for line in lines:
        if not line.startswith("//"):
            break
        block.append(line[2:].strip())
    return "\n".join(block).strip()


def stb_license():
    """The dual MIT / public domain block at the end of the stb headers."""
    lines = (ROOT / "Vendor/stb_image/stb_image.h").read_text(encoding="utf-8").splitlines()
    start = next(i for i, line in enumerate(lines) if "This software is available under 2 licenses" in line)
    end = next(i for i in range(start, len(lines)) if lines[i].strip() == "*/")
    return "\n".join(lines[start:end]).strip()


def slang_license():
    """The bundled compiler is downloaded, not tracked; the text is embedded in case it is absent."""
    bundled = ROOT / "Vendor/tools/slang/mac/aarch64/LICENSE"
    if bundled.exists():
        return bundled.read_text(encoding="utf-8").strip()
    return SLANG_LICENSE


SLANG_LICENSE = """SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.

LLVM Exceptions to the Apache 2.0 License

As an exception, if, as a result of your compiling your source code, portions
of this Software are embedded into an Object form of such source code, you
may redistribute such embedded portions in such Object form without complying
with the conditions of Sections 4(a), 4(b) and 4(d) of the License.

In addition, if you combine or link compiled forms of this Software with
software that is licensed under the GPLv2 ("Combined Software") and if a
court of competent jurisdiction determines that the patent provision (Section
3), the indemnity provision (Section 9) or other Section of the License
conflicts with the conditions of the GPLv2, you may retroactively and
prospectively choose to deem waived or otherwise exclude such Section(s) of
the License, but only in their entirety and only with respect to the Combined
Software."""

CMAKE_LICENSE = """CMake - Cross Platform Makefile Generator
Copyright 2000-2024 Kitware, Inc. and Contributors
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

* Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

* Neither the name of Kitware, Inc. nor the names of Contributors
  may be used to endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."""

GLAD_NOTE = """The OpenGL loader in Vendor/glad was generated by glad 0.1.34. The generated code is released
by the glad project into the public domain, or under WTFPL or CC0 at the user's choice. The
Khronos header KHR/khrplatform.h that comes with it carries the notice below."""

# (name, license, where in the tree, text)
ENTRIES = [
    ("Box2D", "MIT", "Vendor/box2d", lambda: file_text("Vendor/box2d/LICENSE")),
    ("Box3D", "MIT", "Vendor/box3d", lambda: file_text("Vendor/box3d/LICENSE")),
    ("cgltf", "MIT", "Vendor/cgltf", lambda: file_text("Vendor/cgltf/LICENSE")),
    ("dylib", "MIT", "Vendor/dylib", lambda: file_text("Vendor/dylib/LICENSE")),
    ("EnTT", "MIT", "Vendor/entt", lambda: file_text("Vendor/entt/LICENSE")),
    ("glad and the Khronos platform header", "public domain / WTFPL / CC0; the header is MIT", "Vendor/glad",
     lambda: GLAD_NOTE + "\n\n" + comment_block("Vendor/glad/include/KHR/khrplatform.h", "The Khronos Group")),
    ("GLFW", "zlib/libpng", "Vendor/glfw", lambda: file_text("Vendor/glfw/LICENSE.md")),
    ("GLM", "MIT or The Happy Bunny License", "Vendor/glm", lambda: file_text("Vendor/glm/copying.txt")),
    ("SPIRV-Cross", "Apache-2.0", "Vendor/spirv-cross", lambda: file_text("Vendor/spirv-cross/LICENSE")),
    ("stb_image, stb_truetype and stb_image_write", "MIT or public domain, at the user's choice",
     "Vendor/stb_image, Vendor/stb_truetype, Vendor/glfw/deps", stb_license),
    ("Yoga", "MIT", "Vendor/yoga", lambda: file_text("Vendor/yoga/LICENSE")),
    ("RapidJSON", "MIT, with the notices of its bundled parts", "Rain/Vendor/rapidjson",
     lambda: file_text("Rain/Vendor/rapidjson/license.txt")),
    ("yaml-cpp", "MIT", "Rain/Vendor/yaml-cpp", lambda: file_text("Rain/Vendor/yaml-cpp/LICENSE")),
    ("Native File Dialog", "zlib", "Vendor/nfd", lambda: file_text("Vendor/nfd/LICENSE")),
    ("miniz", "MIT", "Vendor/miniz", lambda: file_text("Vendor/miniz/LICENSE")),
    ("ImGuizmo, the origin of the viewport gizmo code", "MIT", "PandaEditor/src/Viewport/Gizmo/Gizmo.cpp",
     lambda: line_comment_header("PandaEditor/src/Viewport/Gizmo/Gizmo.cpp")),
    ("Inter", "SIL Open Font License 1.1", "Panda/resources/default-fonts/Inter",
     lambda: file_text("Panda/resources/default-fonts/Inter/LICENSE.txt")),
    ("JetBrains Mono", "SIL Open Font License 1.1", "Panda/resources/default-fonts/JetBrainsMono",
     lambda: file_text("Panda/resources/default-fonts/JetBrainsMono/LICENSE.txt")),
    ("Slang, the shader compiler bundled with the editor and the SDK", "Apache-2.0 WITH LLVM-exception",
     "Vendor/tools/slang (downloaded by scripts/fetch_shader_tools)", slang_license),
    ("CMake, bundled with the editor and the SDK", "BSD-3-Clause", "copied from the build machine at build time",
     lambda: CMAKE_LICENSE),
]

# Panda's own libraries under the MIT License; their LICENSE.txt files carry one and the same text.
MIT_LIBRARIES = ["Foundation", "PandaAsync", "Fern", "Miren", "Rain", "PandaUI"]

HEADER = """Notices for Panda
=================

Panda is licensed under the Panda Engine License, version 1.0 (LICENSE.txt): its notice and the
full text of the license come first, then the MIT License of its own libraries Foundation,
PandaAsync, Fern, Miren, Rain and PandaUI. The editor, the SDK, the runtime and the games exported with them contain
the third-party components that follow, each under its own license, reproduced here as the licenses
require. This file is generated by scripts/third_party_notices.py from NOTICE,
LICENSE.txt and the license files in the repository; THIRD_PARTY_LICENSES.md is the index with the
location of each one.
"""

def panda_section():
    """The engine's own notice and the full text of its license: the first section of both files."""
    return (f"\n{RULE}\nPanda Engine (Panda Engine License, version 1.0)\nNOTICE, LICENSE.txt\n{RULE}\n\n"
            f"{file_text('NOTICE')}\n\n{file_text('LICENSE.txt')}\n")


def libraries_section():
    """The MIT libraries of Panda: one license text, checked to be the same in every folder."""
    texts = {name: file_text(f"{name}/LICENSE.txt") for name in MIT_LIBRARIES}
    if len(set(texts.values())) != 1:
        raise SystemExit("The LICENSE.txt files of " + ", ".join(MIT_LIBRARIES) + " differ; keep them identical")
    names = ", ".join(MIT_LIBRARIES[:-1]) + " and " + MIT_LIBRARIES[-1]
    return (f"\n{RULE}\nPanda libraries {names} (MIT)\n" + ", ".join(MIT_LIBRARIES) + f"\n{RULE}\n\n"
            f"{texts[MIT_LIBRARIES[0]]}\n")


def render(header):
    parts = [header, panda_section(), libraries_section()]
    for name, license_name, where, text in ENTRIES:
        parts.append(f"\n{RULE}\n{name} ({license_name})\n{where}\n{RULE}\n\n{text()}\n")
    return "\n".join(parts).rstrip() + "\n"


def main():
    (ROOT / "THIRD_PARTY_NOTICES.txt").write_text(render(HEADER), encoding="utf-8")
    print("written THIRD_PARTY_NOTICES.txt")


if __name__ == "__main__":
    main()
