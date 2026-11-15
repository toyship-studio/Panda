// The last pass of the frame: the linear HDR scene becomes the picture on the display. The
// exposure of the camera, its tonemap curve (tonemapParams of PANDA_POST), the sRGB encoding and a
// dither of one display step, so that smooth gradients of the sky do not band in 8 bits.
// Executed by the engine after the user postprocessing chain — never add it to a chain manually.
#include "panda/post.hlsli"

Texture2D inputTexture;
SamplerState inputTextureSampler;

// The PS input mirrors the VS output including SV_POSITION: DXBC links the stages by registers.
struct PSInput {
    float4 position : SV_POSITION;
    float2 fragTexCoord : TEXCOORD0;
};

// AgX (T. Sobotka; the minimal fit by B. Wrensch): the scene goes into a log encoding of 16.5
// stops through a matrix that pulls saturated colors inward, a sigmoid shapes the contrast, and
// the result leaves through the inverse matrix. A bright colored light fades to white instead of
// clipping to a primary. With a mild look on top: the base curve alone is flat for a game.
float3 tonemapAgx(float3 color) {
    color = float3(
        dot(color, float3(0.842479062253094, 0.0784335999999992, 0.0792237451477643)),
        dot(color, float3(0.0423282422610123, 0.878468636469772, 0.0791661274605434)),
        dot(color, float3(0.0423756549057051, 0.0784336, 0.879142973793104))
    );
    const float minEv = -12.47393;
    const float maxEv = 4.026069;
    color = clamp(log2(max(color, 1.0e-10)), minEv, maxEv);
    color = (color - minEv) / (maxEv - minEv);
    const float3 x2 = color * color;
    const float3 x4 = x2 * x2;
    color = 15.5 * x4 * x2 - 40.14 * x4 * color + 31.96 * x4 - 6.868 * x2 * color + 0.4298 * x2 + 0.1191 * color -
            0.00232;
    // The look: a little more contrast and saturation than the base.
    const float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = pow(max(color, 0.0), float3(1.2, 1.2, 1.2));
    color = luma + 1.2 * (color - luma);
    color = float3(
        dot(color, float3(1.19687900512017, -0.0980208811401368, -0.0990297440797205)),
        dot(color, float3(-0.0528968517574562, 1.15190312990417, -0.0989611768448433)),
        dot(color, float3(-0.0529716355144438, -0.0980434501171241, 1.15107367264116))
    );
    // The curve ends in display values of a 2.2 display: back to linear for the common encoding.
    return pow(saturate(color), float3(2.2, 2.2, 2.2));
}

// ACES: the fit of the reference rendering and output transforms by S. Hill.
float3 tonemapAces(float3 color) {
    color = float3(
        dot(color, float3(0.59719, 0.35458, 0.04823)),
        dot(color, float3(0.07600, 0.90834, 0.01566)),
        dot(color, float3(0.02840, 0.13383, 0.83777))
    );
    const float3 a = color * (color + 0.0245786) - 0.000090537;
    const float3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
    color = a / b;
    color = float3(
        dot(color, float3(1.60475, -0.53108, -0.07367)),
        dot(color, float3(-0.10208, 1.10813, -0.00605)),
        dot(color, float3(-0.00327, -0.07276, 1.07602))
    );
    return saturate(color);
}

// Khronos PBR Neutral: colors below 0.8 pass as they are, the highlights above are compressed
// and lose saturation.
float3 tonemapNeutral(float3 color) {
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
    const float low = min(color.r, min(color.g, color.b));
    const float offset = low < 0.08 ? low - 6.25 * low * low : 0.04;
    color -= offset;
    const float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) { return color; }
    const float d = 1.0 - startCompression;
    const float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    const float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return lerp(color, float3(newPeak, newPeak, newPeak), g);
}

float3 encodeSrgb(float3 color) {
    color = saturate(color);
    const float3 low = color * 12.92;
    const float3 high = 1.055 * pow(color, float3(1.0 / 2.4, 1.0 / 2.4, 1.0 / 2.4)) - 0.055;
    return float3(
        color.r <= 0.0031308 ? low.r : high.r, color.g <= 0.0031308 ? low.g : high.g,
        color.b <= 0.0031308 ? low.b : high.b
    );
}

float4 main(PSInput input) : SV_Target0 {
    const float4 source = inputTexture.Sample(inputTextureSampler, input.fragTexCoord);
    float3 color = max(source.rgb, 0.0) * tonemapParams.y;
    const int curve = int(tonemapParams.x + 0.5);
    if (curve == 0) {
        color = tonemapAgx(color);
    } else if (curve == 1) {
        color = tonemapAces(color);
    } else if (curve == 2) {
        color = tonemapNeutral(color);
    } else if (curve == 3) {
        color = color / (color + 1.0);
    }
    color = encodeSrgb(color);
    // Interleaved gradient noise (J. Jimenez), one display step wide.
    const float noise = frac(52.9829189 * frac(dot(input.position.xy, float2(0.06711056, 0.00583715))));
    color += (noise - 0.5) / 255.0;
    // Alpha passes through as is — material previews are drawn on a transparent background.
    return float4(color, source.a);
}
