// A sample of a 3D texture at the point from the UBO — the color fills the whole target: a check
// of the slice layout, the axis order and trilinear filtering along depth. The
// "volume"/"volumeSampler" pair is the Miren binding convention (<name>Sampler).
cbuffer UBO_FRAG {
    float4 sampleCoord; // xyz — volume coordinate [0..1]
};

Texture3D volume;
SamplerState volumeSampler;

float4 main() : SV_TARGET {
    return volume.Sample(volumeSampler, sampleCoord.xyz);
}
