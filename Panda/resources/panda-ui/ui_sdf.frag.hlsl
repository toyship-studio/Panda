// The PS input must mirror the VS output including SV_POSITION: DXBC links the
// stages by register numbers, omitting the position shifts all attributes.
struct PSInput {
    float4 position : SV_POSITION;
    float4 color    : TEXCOORD0;
    float2 localPos : TEXCOORD1;
    float4 params   : TEXCOORD2;
};

// Signed distance field of a rounded rectangle (Inigo Quilez): negative inside,
// 0 on the boundary, positive outside. p — point from the center, b — half-size, r — radius.
float sdRoundBox(float2 p, float2 b, float r) {
    float2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}


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

PSOutput main(PSInput input) {
    float2 halfSize = input.params.xy;
    float radius = input.params.z;
    float border = input.params.w;

    float d = sdRoundBox(input.localPos, halfSize, radius);
    // fwidth(d) — how much the distance changes per 1 screen pixel: antialiasing ~1px wide,
    // automatically correct at any DPI and scale.
    float aa = max(fwidth(d), 1e-5);

    float alpha;
    if (border > 0.0) {
        // Outline inward from the edge, border wide: coverage of the shape minus coverage of the
        // inner shape.
        float outer = clamp(0.5 - d / aa, 0.0, 1.0);
        float inner = clamp(0.5 - (d + border) / aa, 0.0, 1.0);
        alpha = outer - inner;
    } else {
        alpha = clamp(0.5 - d / aa, 0.0, 1.0);
    }

    float4 outColor = input.color;
    outColor.a *= alpha;
    if (outColor.a <= 0.003) {
        discard;
    }
    return makeOutput(outColor);
}
