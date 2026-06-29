// Window Edge Glow — adds a blue glow around all visible window borders
// Params: width = pixel width of glow, intensity = brightness

float4 main(PS_INPUT input) : SV_TARGET {
    float4 color = u_scene.Sample(u_sampler, input.uv);
    float2 pixel = input.uv * u_resolution;

    for (uint i = 0; i < u_window_count && i < 64; i++) {
        float4 wr = u_window_rects[i];
        // wr = (left, top, right, bottom) in pixels

        // Signed distance to nearest edge — negative inside, positive outside
        float2 d_inside  = float2(pixel.x - wr.x, wr.z - pixel.x); // >0 inside
        float2 d_inside2 = float2(pixel.y - wr.y, wr.w - pixel.y);
        float inside_dist = min(min(d_inside.x, d_inside.y), min(d_inside2.x, d_inside2.y));

        float d_left   = max(wr.x - pixel.x, max(0.0f, pixel.x - wr.z));
        float d_top    = max(wr.y - pixel.y, max(0.0f, pixel.y - wr.w));
        float outside_dist = sqrt(d_left * d_left + d_top * d_top);

        float dist = inside_dist >= 0.0f ? -inside_dist : outside_dist;
        // dist < 0 inside window, dist > 0 outside window
        // We want glow around the edge (both sides)

        float glow = exp(-abs(dist) / u_param_width) * u_param_intensity;
        // Cyan-blue glow
        color.rgb += glow * float3(0.15, 0.55, 1.0);
    }

    return saturate(color);
}
