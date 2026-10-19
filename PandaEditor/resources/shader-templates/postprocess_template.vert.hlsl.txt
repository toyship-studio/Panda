// Postprocess pass vertex shader: fullscreen quad passthrough. Usually no changes needed.
struct VSInput {
    float3 position : POSITION;
    float2 texCoord : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float2 fragTexCoord : TEXCOORD0;
};

VSOutput main(VSInput input) {
    VSOutput output;
    output.fragTexCoord = input.texCoord;
    output.position = float4(input.position, 1.0);
    return output;
}
