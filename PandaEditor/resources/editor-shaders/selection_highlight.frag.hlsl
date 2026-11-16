// The output of a viewport: the frame with the outline of the selection over it. The mask holds
// the selected geometry — 1 inside its silhouette, whatever stands in front of it — and the
// outline is the band just outside the silhouette. A viewport that shows no selection gets an
// empty mask and a zero width: the frame goes through as it is.
cbuffer SELECTION_OUTLINE {
    float4 outlineStep; // xy — the width of the outline in uv of the mask; zw — a texel of the mask in uv
};

Texture2D colorTexture;
SamplerState colorTextureSampler;
Texture2D selectedGeometryTexture;
SamplerState selectedGeometryTextureSampler;

// The PS input mirrors the VS output, including SV_POSITION: DXBC links stages by registers.
struct PSInput {
    float4 position : SV_POSITION;
    float2 fragTexCoord : TEXCOORD0;
};

static const float3 OUTLINE_COLOR = float3(1.0, 0.58, 0.05);

float mask(float2 uv) {
    return selectedGeometryTexture.Sample(selectedGeometryTextureSampler, uv).r;
}

float4 main(PSInput input) : SV_Target0 {
    float2 uv = input.fragTexCoord;
    float4 color = colorTexture.Sample(colorTextureSampler, uv);
    // The mask has hard edges. Four taps between the texels give the silhouette an edge a pixel
    // wide, so the inner side of the outline is as smooth as its outer side.
    float2 between = outlineStep.zw * 0.5; // from the center of a texel to its corner
    float inside = 0.25 * (mask(uv + float2(between.x, between.y)) + mask(uv + float2(-between.x, between.y)) +
                           mask(uv + float2(between.x, -between.y)) + mask(uv + float2(-between.x, -between.y)));
    // How close the silhouette comes to the pixel: the mask in eight directions around it, at the
    // width of the outline and at half of it (a thin part of the geometry is not stepped over).
    float reach = inside;
    for (int i = 0; i < 8; i++) {
        float angle = 0.785398 * i;
        float2 offset = float2(cos(angle), sin(angle)) * outlineStep.xy;
        reach = max(reach, mask(uv + offset));
        reach = max(reach, mask(uv + offset * 0.5));
    }
    float outline = saturate(reach - inside);
    return float4(lerp(color.rgb, OUTLINE_COLOR, outline), color.a);
}
