// Generated from the C++ struct Panda::OcclusionUbo (Panda/Renderer/ShaderInterface/ShaderBlocks.hpp):
// PandaSmoke --write-shader-includes <default-shaders/include>. Do not edit by hand: the meaning
// and layout of the fields are described in the struct; new fields go at the end only.
#if !PANDA_FEATURE_OCCLUSION
#error "The 'occlusion' feature is off in this project: turn it on in Project Settings > Rendering, or do not include panda/occlusion.hlsli"
#endif
cbuffer PANDA_OCCLUSION {
    float4 occlusionGrid;
    float4 occlusionWindow;
    float4 occlusionParams;
    float4 occlusionTexel;
    float4 occlusionChunks[64];
    float4 occlusionFocus;
};
Texture3D occlusionVolume;
SamplerState occlusionVolumeSampler;
