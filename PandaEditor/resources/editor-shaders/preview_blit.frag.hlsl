// Copies a texture into an editor thumbnail, preserving the aspect ratio (letterbox): the margins
// are transparent.
// NO tonemap — the source is already display-referred (unlike material previews that come from HDR).
// The exception is an SRGB source: the sample is decoded to linear by hardware, while the display-space
// UI shows the texture, so the inverse encode (encodeSrgb) is applied; otherwise the image darkens.
cbuffer BLIT_FIT {
    float4 uvScaleOffset; // xy — UV scale, zw — offset: output square → source frame
    float4 encodeSrgb;    // x > 0.5: SRGB-format source — convert back linear → sRGB (tonemap curve)
};

Texture2D inputTexture;
SamplerState inputTextureSampler;

struct PSInput {
    float4 position : SV_POSITION;
    float2 fragTexCoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target0 {
    float2 uv = input.fragTexCoord * uvScaleOffset.xy + uvScaleOffset.zw;
    // Outside the source frame — transparent margins (the thumbnail is always square).
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) { return float4(0.0, 0.0, 0.0, 0.0); }
    float4 texel = inputTexture.Sample(inputTextureSampler, uv);
    if (encodeSrgb.x > 0.5) { texel.rgb = pow(abs(texel.rgb), 1.0 / 2.2); }
    return texel;
}
