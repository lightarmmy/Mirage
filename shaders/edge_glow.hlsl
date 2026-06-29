// Cyberpunk Window Edge Glow — neon bloom around window borders
// Params: width = glow radius (px), intensity = neon brightness

static const float3 NEON_COLORS[6] = {
    float3(1.0, 0.1, 0.4),  // hot pink
    float3(0.1, 0.8, 1.0),  // cyan
    float3(0.8, 0.2, 1.0),  // purple
    float3(1.0, 0.6, 0.1),  // orange
    float3(0.2, 1.0, 0.4),  // neon green
    float3(0.3, 0.4, 1.0),  // electric blue
};

float4 main(PS_INPUT input) : SV_TARGET {
    float4 color = u_scene.Sample(u_sampler, input.uv);
    float2 pixel = input.uv * u_resolution;
    float max_range = u_param_width * 6.0;
    float inv_width = 1.0 / max(u_param_width, 1.0);

    float3 glow_total = float3(0, 0, 0);

    for (uint i = 0; i < u_window_count && i < 64; i++) {
        float4 wr = u_window_rects[i];

        float2 closest = clamp(pixel, wr.xy, wr.zw);
        if (length(pixel - closest) > max_range) continue;

        float dl = pixel.x - wr.x, dr = wr.z - pixel.x;
        float dt = pixel.y - wr.y, db = wr.w - pixel.y;
        float inside = min(min(dl, dr), min(dt, db));

        float edge_dist;
        float2 edge_uv;
        if (inside >= 0.0) {
            // Inside window — glow from nearest edge
            edge_dist = inside;
            // Find which edge is closest for color sampling
            if      (dl <= dr && dl <= dt && dl <= db) edge_uv = clamp(pixel + float2( 2, 0), wr.xy, wr.zw);
            else if (dr <= dl && dr <= dt && dr <= db) edge_uv = clamp(pixel + float2(-2, 0), wr.xy, wr.zw);
            else if (dt <= dl && dt <= dr && dt <= db) edge_uv = clamp(pixel + float2(0,  2), wr.xy, wr.zw);
            else                                        edge_uv = clamp(pixel + float2(0, -2), wr.xy, wr.zw);
        } else {
            float dx = dl > 0 ? max(0, -dr) : -dl;
            float dy = dt > 0 ? max(0, -db) : -dt;
            edge_dist = -sqrt(dx * dx + dy * dy);
            edge_uv = closest / u_resolution;
        }

        // Multi-layer neon glow
        float d = abs(edge_dist);
        float core  = exp(-d * inv_width * 0.5);        // bright thin core
        float bloom = exp(-d * inv_width * 0.15) * 0.6;  // wide soft bloom
        float halo  = exp(-d * inv_width * 0.05) * 0.25; // very wide halo
        float glow  = (core + bloom + halo) * u_param_intensity;

        if (glow < 0.002) continue;

        // Sample window edge color, boost saturation for neon look
        float3 edge_color = u_scene.Sample(u_sampler, edge_uv / u_resolution).rgb;
        float luminance = dot(edge_color, float3(0.299, 0.587, 0.114));
        float3 boosted = lerp(edge_color, edge_color * 2.5, 0.7);
        float3 saturated = lerp(float3(luminance, luminance, luminance), boosted, 1.4);

        // Mix with cyberpunk palette based on window index
        float3 neon = NEON_COLORS[i % 6];
        float3 glow_color = lerp(saturated, neon, 0.5);

        glow_total += glow_color * glow;
    }

    // Screen blend for neon look
    color.rgb = color.rgb + glow_total - color.rgb * glow_total;
    return saturate(color);
}
