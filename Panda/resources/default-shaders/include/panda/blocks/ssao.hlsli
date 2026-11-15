// Generated from the C++ struct Panda::SsaoUbo (Panda/Renderer/ShaderInterface/ShaderBlocks.hpp):
// PandaSmoke --write-shader-includes <default-shaders/include>. Do not edit by hand: the meaning
// and layout of the fields are described in the struct; new fields go at the end only.
#if !PANDA_FEATURE_SSAO
#error "The 'ssao' feature is off in this project: turn it on in Project Settings > Rendering, or do not include panda/ssao.hlsli"
#endif
cbuffer PANDA_SSAO {
    float4x4 ssaoViewProj;
    float4 ssaoParams;
};
Texture2D ssaoMap;
SamplerState ssaoMapSampler;
