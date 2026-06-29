// Window Edge Glow — blue glow around visible window borders
// Params: width = glow pixel radius, intensity = brightness

float4 main(PS_INPUT input) : SV_TARGET {
    float4 color = u_scene.Sample(u_sampler, input.uv);
    float2 pixel = input.uv * u_resolution;
    float max_range = u_param_width * 4.0; // skip windows beyond this distance
    float inv_width = 1.0 / max(u_param_width, 1.0);

    for (uint i = 0; i < u_window_count && i < 64; i++) {
        float4 wr = u_window_rects[i];

        // Fast AABB early-out: skip if pixel is far from this window
        float2 closest = clamp(pixel, wr.xy, wr.zw);
        float quick_dist = length(pixel - closest);
        if (quick_dist > max_range) continue;

        // Compute precise distance to nearest edge (signed)
        float left   = pixel.x - wr.x;
        float right  = wr.z - pixel.x;
        float top    = pixel.y - wr.y;
        float bottom = wr.w - pixel.y;

        float inside_x = min(left, right);
        float inside_y = min(top, bottom);
        bool is_inside = (inside_x >= 0.0 && inside_y >= 0.0);

        float dist;
        if (is_inside) {
            dist = min(inside_x, inside_y); // positive = distance inside from edge
        } else {
            // Distance to nearest corner or edge from outside
            float dx = max(-left, max(0.0f, -right));
            float dy = max(-top,  max(0.0f, -bottom));
            dist = -sqrt(dx * dx + dy * dy); // negative = outside
        }

        // Glow on both sides of the edge, fading with distance
        float glow = exp(-abs(dist) * inv_width) * u_param_intensity;
        color.rgb += glow * float3(0.15, 0.55, 1.0);
    }

    return saturate(color);
}
