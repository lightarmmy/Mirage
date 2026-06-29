// Window Edge Jiggle — window borders wobble organically, interior stable
// Params: amount = edge displacement in pixels, speed = oscillation frequency

float hash_noise(uint n) {
    n = (n << 13U) ^ n;
    return float((n * (n * n * 15731U + 789221U) + 1376312589U) & 0x7FFFFFFFU) / 2147483648.0;
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 pixel = input.uv * u_resolution;
    float2 sample_uv = input.uv;
    float total_jiggle = 0.0;
    float2 jiggle_offset = float2(0, 0);

    for (uint i = 0; i < u_window_count && i < 64; i++) {
        float4 wr = u_window_rects[i];

        // Is pixel inside or near this window?
        if (pixel.x < wr.x - u_param_amount || pixel.x > wr.z + u_param_amount ||
            pixel.y < wr.y - u_param_amount || pixel.y > wr.w + u_param_amount) continue;

        // Compute edge distance: 0 at edge, grows toward center, grows outside
        float dl = pixel.x - wr.x;
        float dr = wr.z - pixel.x;
        float dt = pixel.y - wr.y;
        float db = wr.w - pixel.y;

        float edge_dist = min(min(abs(dl), abs(dr)), min(abs(dt), abs(db)));
        bool is_inside = (dl >= 0 && dr >= 0 && dt >= 0 && db >= 0);

        // Jiggle only near edges — fade exponentially toward center/outside
        float influence = exp(-edge_dist / u_param_amount);
        if (influence < 0.01) continue;

        // Per-window phase and multi-frequency wobble
        float phase = hash_noise(i) * 6.2832;
        float wx = (pixel.x - wr.x) / max(wr.z - wr.x, 1.0f);
        float wy = (pixel.y - wr.y) / max(wr.w - wr.y, 1.0f);

        // Primary oscillation perpendicular to nearest edge
        float wave = sin(u_time * u_param_speed * 3.0 + phase)
                   + sin(u_time * u_param_speed * 4.7 + phase * 1.3) * 0.5
                   + sin(u_time * u_param_speed * 7.1 + phase * 0.7) * 0.25;

        float disp = wave * u_param_amount * influence;

        // Displace primarily perpendicular to nearest edge
        float2 dir;
        if      (abs(dl) <= abs(dr) && abs(dl) <= abs(dt) && abs(dl) <= abs(db)) dir = float2(-1, 0);
        else if (abs(dr) <= abs(dl) && abs(dr) <= abs(dt) && abs(dr) <= abs(db)) dir = float2( 1, 0);
        else if (abs(dt) <= abs(dl) && abs(dt) <= abs(dr) && abs(dt) <= abs(db)) dir = float2( 0,-1);
        else                                                                     dir = float2( 0, 1);

        jiggle_offset += dir * disp / u_resolution;
        total_jiggle += influence;
    }

    if (total_jiggle > 0.0) {
        sample_uv = input.uv + jiggle_offset;
    }

    return u_scene.Sample(u_sampler, sample_uv);
}
