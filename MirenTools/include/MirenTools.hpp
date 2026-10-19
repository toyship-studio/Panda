#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using path_t = std::filesystem::path;

namespace MirenTools {

enum ShaderType { VERTEX, FRAGMENT };
enum class GlslTarget { OpenGL, OpenGLES };
enum class MslTarget { MacOS, IOS };

/// slangc for compiling HLSL → SPIR-V: the path is set by the owner of the process (the editor —
/// from the bundle via SystemTools::findSlangExecutable, the smoke test — from CMake). Without the
/// call — "slangc" from PATH.
void setSlangcExecutable(const path_t &path);

/// HLSL includes are a CUSTOM mechanism, a single one for all backends (like fcpp in bgfx shaderc):
/// the compilers (slangc, D3DCompile at runtime) do not see includes and receive self-contained
/// text. `#include "..."` is expanded recursively; the file is looked up next to the including
/// one, then in includeRoots; each file is included once per translation (which also protects
/// against cycles); the insertions are surrounded by `#line` so that compiler errors point into
/// the sources. Macros are not evaluated — an `#include` under `#if` is expanded unconditionally.
/// nullopt — an error (in the log).
std::optional<std::string> expandHlslIncludes(const path_t &input, const std::vector<path_t> &includeRoots);
/// The same into a file; it is written only when the text changes — the cooking chains of the
/// editor and of the export live by mtime, an untouched file does not recompile SPIR-V on every
/// start.
bool expandHlslIncludesToFile(const path_t &input, const path_t &output, const std::vector<path_t> &includeRoots);

/// HLSL (self-contained, see expandHlslIncludes) → SPIR-V with the slangc compiler.
bool compileHlslShaderToSpv(const path_t &input, const path_t &output, ShaderType shaderType);
bool compileSpvShaderToGlsl(
    const path_t &input, const path_t &output, ShaderType shaderType, GlslTarget target = GlslTarget::OpenGL
);
bool compileSpvShaderToMsl(
    const path_t &input, const path_t &output, ShaderType shaderType, MslTarget target = MslTarget::MacOS
);
/// Reflect SpirV shader
bool reflect(const path_t &input, const path_t &output);

} // namespace MirenTools
