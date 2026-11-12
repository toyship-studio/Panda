// Generated from the C++ struct Panda::EnvironmentUbo (Panda/Renderer/ShaderInterface/ShaderBlocks.hpp):
// PandaSmoke --write-shader-includes <default-shaders/include>. Do not edit by hand: the meaning
// and layout of the fields are described in the struct; new fields go at the end only.
#if !PANDA_FEATURE_ENVIRONMENT
#error "The 'environment' feature is off in this project: turn it on in Project Settings > Rendering, or do not include panda/environment.hlsli"
#endif
cbuffer PANDA_ENVIRONMENT {
    float4 environmentParams;
};
Texture2D environmentMap;
SamplerState environmentMapSampler;
