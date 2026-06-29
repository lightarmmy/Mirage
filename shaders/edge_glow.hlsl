// Window Edge Glow — glow matches local window border color
// Params: width = glow pixel radius, intensity = brightness

float4 main(PS_INPUT input) : SV_TARGET {
    float4 color = u_scene.Sample(u_sampler, input.uv);
    float2 pixel = input.uv * u_resolution;
    float max_range = u_param_width * 5.0;
    float inv_width = 1.0 / max(u_param_width, 1.0);

    for (uint i = 0; i < u_window_count && i < 64; i++) {
        float4 wr = u_window_rects[i];

        // Fast AABB early-out
        float2 closest = clamp(pixel, wr.xy, wr.zw);
        if (length(pixel - closest) > max_range) continue;

        // Edge distance (signed)
        float dl = pixel.x - wr.x, dr = wr.z - pixel.x;
        float dt = pixel.y - wr.y, db = wr.w - pixel.y;
        float inside_margin = min(min(dl, dr), min(dt, db));
        bool in_window = (inside_margin >= 0.0);

        float edge_dist;
        if (in_window) {
            edge_dist = inside_margin;
        } else {
            float dx = dl > 0 ? max(0.0f, -dr) : -dl;
            float dy = dt > 0 ? max(0.0f, -db) : -dt;
            edge_dist = -sqrt(dx * dx + dy * dy);
        }

        float glow = exp(-abs(edge_dist) * inv_width) * u_param_intensity;
        if (glow < 0.005) continue;

        // Sample edge color from 3px inside the window
        float2 edge_uv = clamp(pixel, wr.xy + 3.0, wr.zw - 3.0) / u_resolution;
        float3 edge_color = u_scene.Sample(u_sampler, edge_uv).rgb;

        color.rgb = lerp(color.rgb, edge_color, glow);
    }

    return color;
}
