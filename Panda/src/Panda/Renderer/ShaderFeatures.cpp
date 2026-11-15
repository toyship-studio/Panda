#include "Panda/Renderer/ShaderFeatures.hpp"

#include <Foundation/Foundation.hpp>

namespace Panda {

namespace {

    uint32_t bit(ShaderFeature feature) {
        return 1u << static_cast<uint32_t>(feature);
    }

    ShaderFeatureSet s_enabled = ShaderFeatureSet::all();

} // namespace

ShaderFeatureSet ShaderFeatureSet::all() {
    ShaderFeatureSet set;
    set.m_mask = bit(ShaderFeature::Count) - 1u;
    return set;
}

ShaderFeatureSet ShaderFeatureSet::fromNames(const std::vector<std::string> &names, std::string *unknown) {
    ShaderFeatureSet set;
    for (const std::string &name : names) {
        const ShaderFeatureInfo *info = ShaderFeatures::find(name);
        if (info == nullptr) {
            if (unknown != nullptr) {
                if (!unknown->empty()) { *unknown += ", "; }
                *unknown += name;
            }
            continue;
        }
        set.set(info->feature, true);
    }
    return set;
}

bool ShaderFeatureSet::has(ShaderFeature feature) const {
    return (m_mask & bit(feature)) != 0;
}

void ShaderFeatureSet::set(ShaderFeature feature, bool on) {
    if (on) {
        m_mask |= bit(feature);
        for (ShaderFeature required : ShaderFeatures::info(feature).dependencies) { set(required, true); }
    } else {
        m_mask &= ~bit(feature);
        for (const ShaderFeatureInfo &info : ShaderFeatures::catalog()) {
            for (ShaderFeature required : info.dependencies) {
                if (required == feature) { set(info.feature, false); }
            }
        }
    }
}

std::vector<std::string> ShaderFeatureSet::names() const {
    std::vector<std::string> result;
    for (const ShaderFeatureInfo &info : ShaderFeatures::catalog()) {
        if (has(info.feature)) { result.emplace_back(info.name); }
    }
    return result;
}

namespace ShaderFeatures {

    const std::vector<ShaderFeatureInfo> &catalog() {
        static const std::vector<ShaderFeatureInfo> catalog = {
            {ShaderFeature::Lights,
             "lights",
             "Lights",
             "PANDA_FEATURE_LIGHTS",
             "Collects the ambient, the sun and the point lights of the world every frame.",
             {},
             {}},
            {ShaderFeature::Shadows,
             "shadows",
             "Shadows",
             "PANDA_FEATURE_SHADOWS",
             "Shadows of the sun (cascades: up to four depth passes over the world every frame) and of the lamps "
             "that cast them (a pass per spot, six per point light).",
             {{"shadowMap", "Texture2D", "SamplerComparisonState"},
              {"lampShadowMap", "Texture2D", "SamplerComparisonState"}},
             {ShaderFeature::Lights}},
            {ShaderFeature::Occlusion,
             "occlusion",
             "Occlusion Volume",
             "PANDA_FEATURE_OCCLUSION",
             "Occlusion Volume: background bakes of the chunks around the camera and a 3D texture of up to 54 MB.",
             {{"occlusionVolume", "Texture3D", "SamplerState"}},
             {ShaderFeature::Lights}},
            {ShaderFeature::Environment,
             "environment",
             "Environment",
             "PANDA_FEATURE_ENVIRONMENT",
             "The environment map of the world: the default sky draws it and the lit shaders take the light of the "
             "sky from it. One small atlas is baked when the map changes.",
             {{"environmentMap", "Texture2D", "SamplerState"}, {"environmentLighting", "Texture2D", "SamplerState"}},
             {}},
            {ShaderFeature::Wind,
             "wind",
             "Wind",
             "PANDA_FEATURE_WIND",
             "Scene wind: the phases advance every frame and the vegetation sways.",
             {},
             {}},
            {ShaderFeature::Ssao,
             "ssao",
             "Ambient Occlusion (SSAO)",
             "PANDA_FEATURE_SSAO",
             "Screen-space ambient occlusion: two half-resolution passes over the depth of the frame.",
             {{"ssaoMap", "Texture2D", "SamplerState"}},
             {}},
        };
        return catalog;
    }

    const ShaderFeatureInfo &info(ShaderFeature feature) {
        const ShaderFeatureInfo &entry = catalog()[static_cast<size_t>(feature)];
        PND_ASSERT(entry.feature == feature, "SHADER FEATURES: THE CATALOG FOLLOWS THE ORDER OF THE ENUM");
        return entry;
    }

    const ShaderFeatureInfo *find(std::string_view name) {
        for (const ShaderFeatureInfo &info : catalog()) {
            if (name == info.name) { return &info; }
        }
        return nullptr;
    }

    void setEnabled(const ShaderFeatureSet &set) {
        s_enabled = set;
    }

    const ShaderFeatureSet &enabled() {
        return s_enabled;
    }

    bool isEnabled(ShaderFeature feature) {
        return s_enabled.has(feature);
    }

    bool isFeatureTexture(std::string_view name) {
        for (const ShaderFeatureInfo &info : catalog()) {
            for (const ShaderFeatureTexture &texture : info.textures) {
                if (name == texture.name) { return true; }
            }
        }
        return false;
    }

} // namespace ShaderFeatures

} // namespace Panda
