// Window Edge Glow — soft feather glow matching window edge color
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

        // Edge distance (signed: positive inside, negative outside)
        float dl = pixel.x - wr.x, dr = wr.z - pixel.x;
        float dt = pixel.y - wr.y, db = wr.w - pixel.y;
        float inside = min(min(dl, dr), min(dt, db));

        float edge_dist;
        float2 sample_pos;
        if (inside >= 0.0) {
            edge_dist = inside;
            // Sample color 3px inside from nearest edge
            if      (dl <= dr && dl <= dt && dl <= db) sample_pos = float2(wr.x + 3, pixel.y);
            else if (dr <= dl && dr <= dt && dr <= db) sample_pos = float2(wr.z - 3, pixel.y);
            else if (dt <= dl && dt <= dr && dt <= db) sample_pos = float2(pixel.x, wr.y + 3);
            else                                        sample_pos = float2(pixel.x, wr.w - 3);
        } else {
            float dx = dl > 0 ? max(0, -dr) : -dl;
            float dy = dt > 0 ? max(0, -db) : -dt;
            edge_dist = -sqrt(dx * dx + dy * dy);
            sample_pos = closest;
        }

        float glow = exp(-abs(edge_dist) * inv_width) * u_param_intensity;
        if (glow < 0.005) continue;

        // Sample edge color from the captured desktop
        float3 edge_color = u_scene.Sample(u_sampler, clamp(sample_pos, float2(0,0), u_resolution) / u_resolution).rgb;

        color.rgb = lerp(color.rgb, edge_color, glow);
    }

    return color;
}
