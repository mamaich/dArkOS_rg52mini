#version 450
// Rotate + SR for a rectangle that is not the upscaled frame's size (fit, fill, a TV): inside it one bilinear
// sample of the frame sr_decode.comp built (once a new NPU output), outside it as rotate.frag; the program's
// bars as sr_rotate.frag has them. sr_rotate.frag's push constants (rotate.vert reads rot).
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(set = 0, binding = 0) uniform sampler2D app;
layout(set = 1, binding = 0) uniform sampler2D rgb;
layout(push_constant) uniform PC { int rot, AW, AH, ox, oy, W, H, S, wstride, zp; float scale; int B, GH, A, dx, dy, DW, DH, OW, OH, moved; } p;

void main()
{
    ivec2 a = ivec2(floor(uv * vec2(p.AW, p.AH)));
    ivec2 d = a - ivec2(p.dx, p.dy);
    if (d.x < 0 || d.y < 0 || d.x >= p.DW || d.y >= p.DH) {
        color = texture(app, uv);
        return;
    }
    if (p.moved != 0) {
        ivec2 r = a - ivec2(p.ox, p.oy);
        if (r.x < 0 || r.y < 0 || r.x >= p.W * p.A || r.y >= p.H * p.A) {
            vec4 c = texture(app, uv);
            if (any(greaterThan(c.rgb, vec3(1.5 / 255.0)))) {
                color = c;
                return;
            }
        }
    }
    color = vec4(texture(rgb, (vec2(d) + 0.5) / vec2(p.DW, p.DH)).rgb, 1.0);
}
