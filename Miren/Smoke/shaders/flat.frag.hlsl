// Green fill: drawn with MIREN_STATE_NO_COLOR_WRITE — it must not appear on the target.
float4 main() : SV_TARGET {
    return float4(0.0, 1.0, 0.0, 1.0);
}
