// Generated from the C++ struct Panda::LightsUbo (Panda/Renderer/ShaderInterface/ShaderBlocks.hpp):
// PandaSmoke --write-shader-includes <default-shaders/include>. Do not edit by hand: the meaning
// and layout of the fields are described in the struct; new fields go at the end only.
#if !PANDA_FEATURE_LIGHTS
#error "The 'lights' feature is off in this project: turn it on in Project Settings > Rendering, or do not include panda/lights.hlsli"
#endif
cbuffer PANDA_LIGHTS {
    float4 ambientColorIntensity;
    float4 sunDirectionIntensity;
    float4 sunColor;
    uint4 lightCounts;
    float4 pointLightPositionsRange[8];
    float4 pointLightColorsIntensity[8];
};
