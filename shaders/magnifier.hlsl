float4 main(PS_INPUT input) : SV_TARGET {
    float2 mouse_uv = u_mouse / u_resolution;
    float2 delta = input.uv - mouse_uv;
    float dist = length(delta * float2(u_resolution.x / u_resolution.y, 1.0));
    float radius_uv = u_param_radius / u_resolution.x;

    if (dist < radius_uv) {
        float t = 1.0 - smoothstep(0.0, radius_uv, dist);
        float zoom = lerp(1.0, u_param_zoom, t);
        float2 sample_uv = mouse_uv + delta / zoom;
        return u_scene.Sample(u_sampler, sample_uv);
    }

    return u_scene.Sample(u_sampler, input.uv);
}
