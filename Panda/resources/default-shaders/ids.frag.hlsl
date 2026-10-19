cbuffer MATERIAL_FIELDS {
    uint id;
};

// The PS input mirrors the VS output including SV_POSITION: DXBC links the stages by registers.
struct PSInput {
    float4 position : SV_POSITION;
    nointerpolation uint fragId : TEXCOORD2;
};

uint main(PSInput input) : SV_Target0 {
    if (id != 0u) {
        return id;
    }
    return input.fragId;
}
