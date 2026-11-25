#version 450
// sr_nat: a native pixel as (Cr, Cb, block of one colour, Y8). The colour and the flag are what sr_prep.comp
// writes; the luma is quantised here by the same expression, so sr_nin_s2d.comp packs the same bytes for the NPU.
// A colour attachment, not a storage image: the driver compresses it (AFBC) and writes it from the tile buffer.
layout(location = 0) in vec2 uvA;
layout(location = 1) in vec2 uvB;
layout(location = 2) in vec2 uvC;
layout(location = 0) out vec4 color;
layout(set = 0, binding = 0) uniform sampler2D app;
layout(constant_id = 0) const int S = 3;   // the step the program shows the native frame at

void main()
{
    vec3 rgb = texture(app, uvA).rgb;
    // (S-1, S-2): odd, against the first, so a one-pixel dither shows, and by the far corner; a step of 2 (the PSP)
    // also takes the far corner itself, else a horizontal stroke through the block would pass
    vec3 d = abs(texture(app, uvB).rgb - rgb);
    if (S < 3) d = max(d, abs(texture(app, uvC).rgb - rgb));
    float f = any(greaterThan(d, vec3(1.5 / 255.0))) ? 0.0 : 1.0;
    float yl = dot(rgb, vec3(0.299, 0.587, 0.114));
    color = vec4((rgb.r - yl) * 0.713 + 0.5, (rgb.b - yl) * 0.564 + 0.5, f, round(clamp(yl, 0.0, 1.0) * 255.0) / 255.0);
}
