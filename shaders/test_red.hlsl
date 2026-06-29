// DIAGNOSTIC: renders solid red to test if full-screen coverage works
float4 main(PS_INPUT input) : SV_TARGET {
    return float4(1.0, 0.0, 0.0, 1.0);
}
