// Temperature range: ~1000K (warm/red) to ~12000K (cool/blue)
// Reference white point adjustments via RGB gain

float3 temperature_to_gain(float temp_k) {
    // Clamp to range
    temp_k = clamp(temp_k, 1000.0, 12000.0);
    // Normalize 0..1 where 0=warm, 1=cool, 0.5=neutral (6500K)
    float t = (temp_k - 6500.0) / 6500.0;

    // Warm shift: boost red, reduce blue
    // Cool shift: boost blue, reduce red
    float3 warm = float3(1.15, 0.85, 0.65);
    float3 cool = float3(0.65, 0.85, 1.15);
    float3 neutral = float3(1.0, 1.0, 1.0);

    if (t < 0.0) return lerp(neutral, warm, -t);
    else         return lerp(neutral, cool,  t);
}

float4 main(PS_INPUT input) : SV_TARGET {
    float4 color = u_scene.Sample(u_sampler, input.uv);
    float3 gain = temperature_to_gain(u_param_temperature);
    color.rgb *= gain;
    return color;
}
