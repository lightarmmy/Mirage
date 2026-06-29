float4 main(PS_INPUT input) : SV_TARGET {
    float4 color = u_scene.Sample(u_sampler, input.uv);
    color.rgb = 1.0 - color.rgb;
    return color;
}
