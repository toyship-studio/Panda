// Generated from the C++ struct Panda::SceneLightsUbo (Panda/Renderer/ShaderInterface/ShaderBlocks.hpp):
// PandaSmoke --write-shader-includes <default-shaders/include>. Do not edit by hand: the meaning
// and layout of the fields are described in the struct; new fields go at the end only.
cbuffer SCENE_LIGHTS {
    float4 ambientColorIntensity;
    float4 directionalDirectionIntensity;
    float4 directionalColor;
    uint4 sceneLightCounts;
    float4 pointLightPositionsRange[8];
    float4 pointLightColorsIntensity[8];
    float4 cameraPosition;
    float4x4 shadowMatrices[4];
    float4 shadowSplits;
    float4 shadowParams;
    float4 shadowTexelWorld;
    float4 shadowDepthScale;
    float4 shadowBias;
    float4 occlusionGrid;
    float4 occlusionWindow;
    float4 occlusionParams;
    float4 occlusionTexel;
    float4 occlusionChunks[64];
    float4 environmentParams;
    float4 occlusionFocus;
};
