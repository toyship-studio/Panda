// Generated from the C++ struct Panda::ShadowsUbo (Panda/Renderer/ShaderInterface/ShaderBlocks.hpp):
// PandaSmoke --write-shader-includes <default-shaders/include>. Do not edit by hand: the meaning
// and layout of the fields are described in the struct; new fields go at the end only.
#if !PANDA_FEATURE_SHADOWS
#error "The 'shadows' feature is off in this project: turn it on in Project Settings > Rendering, or do not include panda/shadows.hlsli"
#endif
cbuffer PANDA_SHADOWS {
    float4x4 shadowMatrices[4];
    float4 shadowSplits;
    float4 shadowParams;
    float4 shadowTexelWorld;
    float4 shadowDepthScale;
    float4 shadowBias;
};
Texture2D shadowMap;
SamplerComparisonState shadowMapSampler;
