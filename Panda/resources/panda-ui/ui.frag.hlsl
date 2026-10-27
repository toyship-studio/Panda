Texture2D u_texture;
SamplerState u_textureSampler;

// The rounded clip of the command (DrawCommand::mask): the rect x,y,w,h in UI points, the radii of
// the corners (top left, top right, bottom right, bottom left), params.x 1 when a mask is set.
cbuffer MASK {
    float4 maskRect;
    float4 maskRadii;
    float4 maskParams;
};

// The PS input must mirror the VS output including SV_POSITION: DXBC links the
// stages by register numbers, omitting the position shifts all attributes.
struct PSInput {
    float4 position : SV_POSITION;
    float4 fragColor : TEXCOORD0;
    float2 fragTexCoord : TEXCOORD1;
    float2 maskPos : TEXCOORD2;
};


// Scene MRT: the second output is the emissive buffer (the glow contribution; there is none here).
struct PSOutput {
    float4 color : SV_Target0;
    float4 emissive : SV_Target1;
};

PSOutput makeOutput(float4 color) {
    PSOutput output;
    output.color = color;
    output.emissive = float4(0.0, 0.0, 0.0, 0.0);
    return output;
}

// Signed distance to a rounded rectangle: negative inside, 0 on the boundary, positive outside.
float sdRoundBox(float2 p, float2 b, float r) {
    float2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// Signed distance to a rectangle with a radius per corner (Inigo Quilez): the radius of the
// quadrant the point lies in.
float sdRoundedBoxCorners(float2 p, float2 b, float4 r) {
    float2 rightSide = (p.x > 0.0) ? r.yz : r.xw; // top right, bottom right : top left, bottom left
    float radius = (p.y > 0.0) ? rightSide.y : rightSide.x;
    float2 q = abs(p) - b + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

// The coverage of the rounded clip at this pixel: 1 inside, an antialiased edge, 0 outside.
float maskCoverage(float2 maskPos) {
    if (maskParams.x < 0.5) {
        return 1.0;
    }
    float2 center = maskRect.xy + maskRect.zw * 0.5;
    float d = sdRoundedBoxCorners(maskPos - center, maskRect.zw * 0.5, maskRadii);
    float aa = max(fwidth(d), 1e-5);
    return clamp(0.5 - d / aa, 0.0, 1.0);
}

PSOutput main(PSInput input) {
    float4 outColor = input.fragColor;
    outColor *= u_texture.Sample(u_textureSampler, input.fragTexCoord);
    outColor.a *= maskCoverage(input.maskPos);
    if (outColor.a <= 0.01) {
        discard;
    }
    return makeOutput(outColor);
}
