// Window Jiggle — windows wobble independently with a sinusoidal wave
// Params: amount = pixel displacement, speed = wobble frequency

float hash(uint n) {
    n = (n << 13U) ^ n;
    return float((n * (n * n * 15731U + 789221U) + 1376312589U) & 0x7FFFFFFFU) / 2147483648.0;
}

float4 main(PS_INPUT input) : SV_TARGET {
    float2 pixel = input.uv * u_resolution;
    float2 sample_uv = input.uv;

    // Check all windows — first one containing this pixel wins
    for (uint i = 0; i < u_window_count && i < 64; i++) {
        float4 wr = u_window_rects[i];

        if (pixel.x >= wr.x && pixel.x <= wr.z && pixel.y >= wr.y && pixel.y <= wr.w) {
            // This pixel is inside a window — apply jiggle
            float phase = hash(i) * 6.2832; // unique phase per window
            float wx = (pixel.x - wr.x) / max(wr.z - wr.x, 1.0f);
            float wy = (pixel.y - wr.y) / max(wr.w - wr.y, 1.0f);

            // Multi-frequency wobble for organic feel
            float jiggle_x = (sin(wy * 8.0 + u_time * u_param_speed + phase) * 0.6
                           +  cos(wx * 11.0 + u_time * 1.7 * u_param_speed + phase) * 0.4)
                           * u_param_amount / u_resolution.x;

            float jiggle_y = (cos(wx * 7.0 + u_time * u_param_speed * 1.3 + phase) * 0.6
                           +  sin(wy * 10.0 + u_time * 1.5 * u_param_speed + phase) * 0.4)
                           * u_param_amount / u_resolution.y;

            sample_uv = input.uv + float2(jiggle_x, jiggle_y);
            break;
        }
    }

    return u_scene.Sample(u_sampler, sample_uv);
}
