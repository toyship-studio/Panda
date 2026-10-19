cbuffer UBO2 {
    float4 gCameraWorldPos;
};

// The PS input mirrors the VS output including SV_POSITION: DXBC links the stages by registers.
struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
};

static const float gGridSize = 100.0;
static const float gGridMinPixelsBetweenCells = 1.0;
static const float gGridCellSize = 0.05;
static const float4 gGridColorThin = float4(0.5, 0.5, 0.5, 0.3);
static const float4 gGridColorThick = float4(0.0, 0.0, 0.0, 0.6);

float log10_custom(float value) {
    return log(value) / log(10.0);
}

float satf(float value) {
    return saturate(value);
}

float2 satv(float2 value) {
    return saturate(value);
}

float max2(float2 value) {
    return max(value.x, value.y);
}

float2 positiveModulo(float2 value, float divisor) {
    return value - divisor * floor(value / divisor);
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
    float2 dvx = float2(ddx(input.worldPos.x), ddy(input.worldPos.x));
    float2 dvy = float2(ddx(input.worldPos.z), ddy(input.worldPos.z));

    float2 dudv = float2(length(dvx), length(dvy));
    float lodLength = length(dudv);
    float lod = max(0.0, log10_custom(lodLength * gGridMinPixelsBetweenCells / gGridCellSize) + 1.0);

    float gridCellSizeLod0 = gGridCellSize * pow(10.0, floor(lod));
    float gridCellSizeLod1 = gridCellSizeLod0 * 10.0;
    float gridCellSizeLod2 = gridCellSizeLod1 * 10.0;

    dudv *= 4.0;

    float2 modDivDudv = positiveModulo(input.worldPos.xz, gridCellSizeLod0) / dudv;
    float lod0a = max2(1.0.xx - abs(satv(modDivDudv) * 2.0 - 1.0.xx));

    modDivDudv = positiveModulo(input.worldPos.xz, gridCellSizeLod1) / dudv;
    float lod1a = max2(1.0.xx - abs(satv(modDivDudv) * 2.0 - 1.0.xx));

    modDivDudv = positiveModulo(input.worldPos.xz, gridCellSizeLod2) / dudv;
    float lod2a = max2(1.0.xx - abs(satv(modDivDudv) * 2.0 - 1.0.xx));

    float lodFade = frac(lod);
    float4 color;

    if (lod2a > 0.0) {
        color = gGridColorThick;
        color.a *= lod2a;
    } else if (lod1a > 0.0) {
        color = lerp(gGridColorThick, gGridColorThin, lodFade);
        color.a *= lod1a;
    } else {
        color = gGridColorThin;
        color.a *= lod0a * (1.0 - lodFade);
    }

    float opacityFalloff = 1.0 - satf(length(input.worldPos.xz - gCameraWorldPos.xz) / gGridSize);
    color.a *= opacityFalloff;
    // HDR: the scene buffer is linear light; the authored grid color is sRGB.
    color.rgb = pow(abs(color.rgb), 2.2);
    return makeOutput(color);
}
