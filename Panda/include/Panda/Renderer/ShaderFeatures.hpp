#pragma once

#include <Rain/Rain.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Panda {

// Shader features (context/shader-features.md): the optional scene data a project enables for its
// materials — lights, shadows, occlusion, environment, wind. A feature owns one HLSL block (its
// struct in ShaderBlocks.hpp, generated into `panda/blocks/<feature>.hlsli`), its system textures,
// the render work that fills the block, and the code of the shared includes guarded by
// `PANDA_FEATURE_<FEATURE>`. The project settings list the enabled features. A shader is compiled
// with the macros of its set in front of it (ShaderInterface::featuresInclude): the set of the
// project in the editor and the export, every feature in the bundle's own bake. A feature that is
// off is not computed and not compiled into the shaders of the project; its block still reaches
// every draw with the data of an empty scene, so a program compiled with the feature stays
// correct. The names are a contract fixed for every version of the engine: a block grows at its
// end only, a new capability is a new feature.
enum class ShaderFeature : uint8_t { Lights, Shadows, Occlusion, Environment, Wind, Count };

// A system texture of a feature: declared by the generated include, bound by the renderer to every
// scene draw (a placeholder when the frame has none), never a material field.
struct ShaderFeatureTexture final {
    const char *name;        // "shadowMap"
    const char *textureType; // HLSL: "Texture2D", "Texture3D"
    const char *samplerType; // HLSL: "SamplerState", "SamplerComparisonState"
};

struct ShaderFeatureInfo final {
    ShaderFeature feature;
    const char *name;  // "shadows": the key in the settings, the include `panda/shadows.hlsli`
    const char *macro; // "PANDA_FEATURE_SHADOWS": 1 or 0 in front of the shader
    const char *cost;  // one line for the editor: what the feature costs while it is on
    std::vector<ShaderFeatureTexture> textures;
    std::vector<ShaderFeature> dependencies; // features this one reads (shadows need the sun of lights)
};

// A set of enabled features.
class ShaderFeatureSet final {
public:
    ShaderFeatureSet() = default; // no feature
    static ShaderFeatureSet all();
    // From the names of a settings list ("lights", "shadows", …); an unknown name is skipped and
    // appended to `unknown` when it is given. The requirements of the listed features are on too.
    static ShaderFeatureSet fromNames(const std::vector<std::string> &names, std::string *unknown = nullptr);
    bool has(ShaderFeature feature) const;
    // On: the requirements of the feature come along; off: the features that require it go too.
    void set(ShaderFeature feature, bool on);
    // The features that are on, in catalog order (the settings list).
    std::vector<std::string> names() const;
    bool operator==(const ShaderFeatureSet &other) const {
        return m_mask == other.m_mask;
    }
    bool operator!=(const ShaderFeatureSet &other) const {
        return m_mask != other.m_mask;
    }

private:
    uint32_t m_mask = 0;
};

namespace ShaderFeatures {

    const std::vector<ShaderFeatureInfo> &catalog(); // in the order of the enum
    const ShaderFeatureInfo &info(ShaderFeature feature);
    const ShaderFeatureInfo *find(std::string_view name);
    // The enabled set of the process: the project in the editor, the manifest in a game. Every
    // feature by default — an application without a project (the smoke test, a bare app).
    void setEnabled(const ShaderFeatureSet &set);
    const ShaderFeatureSet &enabled();
    bool isEnabled(ShaderFeature feature);
    // A system texture of some feature (the reflection mapper keeps it out of the material fields).
    bool isFeatureTexture(std::string_view name);

} // namespace ShaderFeatures

// The "rendering" section of the project scope (`.Panda/project.json`): the features of the
// project. The default is every feature of the catalog, so a project without the section keeps
// its look; the project template of the editor writes the lean set. The editor applies a change
// on save of the project settings (the shaders of the project recompile); the export writes the
// set into the game's manifest.
struct ProjectRenderingSettings final : public Rain::Codable {
    std::vector<std::string> features = ShaderFeatureSet::all().names();

    RAIN_FIELDS_BEGIN(ProjectRenderingSettings)
    RAIN_FIELD(features)
    RAIN_FIELDS_END
};

} // namespace Panda
