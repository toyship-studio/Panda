// Antialiasing of the finished frame (FXAA 3.11 by T. Lottes, the quality path): the pass after
// the tonemap, on display colors. Where the contrast of the brightness around a pixel says
// "edge", the edge is followed both ways to its ends, and the pixel is blended with its neighbor
// across the edge by how far along the stair step it stands; a single bright or dark pixel is
// softened by its neighborhood. One texture, no history: nothing ghosts, thin geometry far away
// still shimmers.
#include "panda/post.hlsli"

Texture2D inputTexture;
SamplerState inputTextureSampler;

struct PSInput {
    float4 position : SV_POSITION;
    float2 fragTexCoord : TEXCOORD0;
};

static const float EDGE_THRESHOLD = 0.125;     // the contrast that makes an edge, relative to the brightest neighbor
static const float EDGE_THRESHOLD_MIN = 0.0312; // … and absolute: dark areas are left alone
static const float SUBPIXEL = 0.75;             // how much a lone pixel is softened
static const int SEARCH_STEPS = 10;
static const float SEARCH_STEP[10] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 4.0, 8.0};

float luma(float3 color) {
    return dot(color, float3(0.299, 0.587, 0.114));
}

float lumaAt(float2 uv) {
    return luma(inputTexture.SampleLevel(inputTextureSampler, uv, 0.0).rgb);
}

float4 main(PSInput input) : SV_Target0 {
    const float2 uv = input.fragTexCoord;
    const float2 texel = 1.0 / resolutionTime.xy;
    const float4 center = inputTexture.SampleLevel(inputTextureSampler, uv, 0.0);
    const float lumaCenter = luma(center.rgb);
    const float lumaDown = lumaAt(uv + float2(0.0, texel.y));
    const float lumaUp = lumaAt(uv - float2(0.0, texel.y));
    const float lumaLeft = lumaAt(uv - float2(texel.x, 0.0));
    const float lumaRight = lumaAt(uv + float2(texel.x, 0.0));
    const float lumaMin = min(lumaCenter, min(min(lumaDown, lumaUp), min(lumaLeft, lumaRight)));
    const float lumaMax = max(lumaCenter, max(max(lumaDown, lumaUp), max(lumaLeft, lumaRight)));
    const float range = lumaMax - lumaMin;
    if (range < max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD)) { return center; }

    const float lumaDownLeft = lumaAt(uv + float2(-texel.x, texel.y));
    const float lumaUpRight = lumaAt(uv + float2(texel.x, -texel.y));
    const float lumaUpLeft = lumaAt(uv + float2(-texel.x, -texel.y));
    const float lumaDownRight = lumaAt(uv + float2(texel.x, texel.y));
    const float lumaDownUp = lumaDown + lumaUp;
    const float lumaLeftRight = lumaLeft + lumaRight;
    const float lumaLeftCorners = lumaDownLeft + lumaUpLeft;
    const float lumaDownCorners = lumaDownLeft + lumaDownRight;
    const float lumaRightCorners = lumaDownRight + lumaUpRight;
    const float lumaUpCorners = lumaUpRight + lumaUpLeft;
    // The edge runs along the axis across which the brightness changes more.
    const float edgeHorizontal = abs(-2.0 * lumaLeft + lumaLeftCorners) + abs(-2.0 * lumaCenter + lumaDownUp) * 2.0 +
                                 abs(-2.0 * lumaRight + lumaRightCorners);
    const float edgeVertical = abs(-2.0 * lumaUp + lumaUpCorners) + abs(-2.0 * lumaCenter + lumaLeftRight) * 2.0 +
                               abs(-2.0 * lumaDown + lumaDownCorners);
    const bool horizontal = edgeHorizontal >= edgeVertical;

    // The two neighbors across the edge; the edge lies on the side of the steeper one.
    const float luma1 = horizontal ? lumaUp : lumaLeft;
    const float luma2 = horizontal ? lumaDown : lumaRight;
    const float gradient1 = luma1 - lumaCenter;
    const float gradient2 = luma2 - lumaCenter;
    const bool steepest1 = abs(gradient1) >= abs(gradient2);
    const float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));
    float stepLength = horizontal ? texel.y : texel.x;
    float lumaLocalAverage;
    if (steepest1) {
        stepLength = -stepLength;
        lumaLocalAverage = 0.5 * (luma1 + lumaCenter);
    } else {
        lumaLocalAverage = 0.5 * (luma2 + lumaCenter);
    }
    // Walk along the edge, half a texel across it (a bilinear read averages the two sides).
    float2 edgeUv = uv;
    if (horizontal) {
        edgeUv.y += stepLength * 0.5;
    } else {
        edgeUv.x += stepLength * 0.5;
    }
    const float2 along = horizontal ? float2(texel.x, 0.0) : float2(0.0, texel.y);
    float2 uv1 = edgeUv - along;
    float2 uv2 = edgeUv + along;
    float lumaEnd1 = lumaAt(uv1) - lumaLocalAverage;
    float lumaEnd2 = lumaAt(uv2) - lumaLocalAverage;
    bool reached1 = abs(lumaEnd1) >= gradientScaled;
    bool reached2 = abs(lumaEnd2) >= gradientScaled;
    for (int i = 1; i < SEARCH_STEPS && !(reached1 && reached2); ++i) {
        if (!reached1) {
            uv1 -= along * SEARCH_STEP[i];
            lumaEnd1 = lumaAt(uv1) - lumaLocalAverage;
            reached1 = abs(lumaEnd1) >= gradientScaled;
        }
        if (!reached2) {
            uv2 += along * SEARCH_STEP[i];
            lumaEnd2 = lumaAt(uv2) - lumaLocalAverage;
            reached2 = abs(lumaEnd2) >= gradientScaled;
        }
    }
    // The nearer end says how far along the stair step the pixel stands.
    const float distance1 = horizontal ? uv.x - uv1.x : uv.y - uv1.y;
    const float distance2 = horizontal ? uv2.x - uv.x : uv2.y - uv.y;
    const bool nearer1 = distance1 < distance2;
    const float edgeLength = distance1 + distance2;
    float pixelOffset = 0.5 - min(distance1, distance2) / edgeLength;
    // … unless the brightness at that end changes the same way as at the pixel: then the pixel is
    // not on this step.
    const bool centerSmaller = lumaCenter < lumaLocalAverage;
    const bool correct = ((nearer1 ? lumaEnd1 : lumaEnd2) < 0.0) != centerSmaller;
    if (!correct) { pixelOffset = 0.0; }
    // A lone pixel against its neighborhood.
    const float lumaAverage = (1.0 / 12.0) * (2.0 * (lumaDownUp + lumaLeftRight) + lumaLeftCorners + lumaRightCorners);
    const float subpixel1 = saturate(abs(lumaAverage - lumaCenter) / range);
    const float subpixel2 = (-2.0 * subpixel1 + 3.0) * subpixel1 * subpixel1;
    pixelOffset = max(pixelOffset, subpixel2 * subpixel2 * SUBPIXEL);

    float2 finalUv = uv;
    if (horizontal) {
        finalUv.y += pixelOffset * stepLength;
    } else {
        finalUv.x += pixelOffset * stepLength;
    }
    return float4(inputTexture.SampleLevel(inputTextureSampler, finalUv, 0.0).rgb, center.a);
}
