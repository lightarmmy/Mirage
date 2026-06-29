// Window Edge Jiggle — borders wobble with multi-frequency oscillation
// Params: amount = max edge displacement (pixels), speed = wobble frequency

float hash_noise(uint n) {
    n = (n << 13U) ^ n;
    return float((n * (n * n * 15731U + 789221U) + 1376312589U) & 0x7FFFFFFFU) / 2147483648.0;
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 pixel = input.uv * u_resolution;
    float2 disp = float2(0, 0);
    float total_w = 0.0;

    for (uint i = 0; i < u_window_count && i < 64; i++) {
        float4 wr = u_window_rects[i];

        // Quick AABB rejection (include jiggle range)
        if (pixel.x < wr.x - u_param_amount * 3 || pixel.x > wr.z + u_param_amount * 3 ||
            pixel.y < wr.y - u_param_amount * 3 || pixel.y > wr.w + u_param_amount * 3) continue;

        // Distance to each edge
        float dl = pixel.x - wr.x, dr = wr.z - pixel.x;
        float dt = pixel.y - wr.y, db = wr.w - pixel.y;

        // Edge influence falls off with distance from edge
        float e_left   = exp(-abs(dl) / u_param_amount);
        float e_right  = exp(-abs(dr) / u_param_amount);
        float e_top    = exp(-abs(dt) / u_param_amount);
        float e_bottom = exp(-abs(db) / u_param_amount);

        float phase = hash_noise(i) * 6.2832;

        // Multi-frequency wobble
        float wave_l = sin(u_time * u_param_speed * 2.5 + phase)
                     + cos(u_time * u_param_speed * 4.0 + phase * 1.7) * 0.6;

        float wave_r = sin(u_time * u_param_speed * 2.5 + phase + 0.5)
                     + cos(u_time * u_param_speed * 4.0 + phase * 1.7 + 0.5) * 0.6;

        float wave_t = sin(u_time * u_param_speed * 2.8 + phase + 1.0)
                     + cos(u_time * u_param_speed * 3.5 + phase * 1.3 + 1.0) * 0.6;

        float wave_b = sin(u_time * u_param_speed * 2.8 + phase + 1.5)
                     + cos(u_time * u_param_speed * 3.5 + phase * 1.3 + 1.5) * 0.6;

        // Displace perpendicular to edge
        disp.x += (-wave_l * e_left + wave_r * e_right) * u_param_amount / u_resolution.x;
        disp.y += (-wave_t * e_top + wave_b * e_bottom) * u_param_amount / u_resolution.y;
        total_w += e_left + e_right + e_top + e_bottom;
    }

    float2 sample_uv = input.uv;
    if (total_w > 0.01) {
        sample_uv = input.uv + disp;
    }

    return u_scene.Sample(u_sampler, sample_uv);
}
