#version 450
// Rotate + SR: inside the rectangle (dx,dy, DW x DH) a pixel is built from the NPU's output (luma, NC1HWC2
// C2=16, channel dy*B+dx of a cell; the upscaled frame is OW x OH, resampled bilinearly when the rectangle
// is another size) and the native frame's colour (Cr,Cb, bilinear as align_corners=False); outside it as
// rotate.frag. The program's own native frame is the A*W x A*H area at ox,oy.
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(set = 0, binding = 0) uniform sampler2D app;
layout(std430, set = 1, binding = 0) readonly buffer Nout { uint w[]; } nout;
layout(set = 1, binding = 1) uniform sampler2D cbcr;
layout(std430, set = 1, binding = 2) readonly buffer Cnt { uint n; } cnt;   // blocks not one colour (sr_prep.comp)
// B: output pixels a side of one NPU cell (S for ESPCN row5, 2S for the space-to-depth models, S*K for cells);
// GH: the NPU grid's height (NC1HWC2: channel c lives in the group c>>4, each group GH rows of wstride cells);
// over: with more blocks than this not one colour, the program's picture as it is (0: never)
layout(push_constant) uniform PC { int rot, AW, AH, ox, oy, W, H, S, wstride, zp; float scale; int B, GH, A, dx, dy, DW, DH, OW, OH, over; } p;

// a pixel of the upscaled frame (clamped to it)
float luma(ivec2 r)
{
    r = clamp(r, ivec2(0), ivec2(p.OW - 1, p.OH - 1));
    int gh = r.y / p.B, gw = r.x / p.B;
    int c = (r.y - gh * p.B) * p.B + (r.x - gw * p.B);
    uint word = nout.w[(((c >> 4) * p.GH + gh) * p.wstride + gw) * 4 + ((c & 15) >> 2)];
    int q = bitfieldExtract(int(word), (c & 3) * 8, 8);
    return clamp((float(q) - float(p.zp)) * p.scale, 0.0, 1.0);
}

void main()
{
    ivec2 a = ivec2(floor(uv * vec2(p.AW, p.AH)));
    ivec2 d = a - ivec2(p.dx, p.dy);
    // a menu over the frame, or a frame that is not the native one enlarged as expected (sr_swap_build): all as drawn
    if (d.x < 0 || d.y < 0 || d.x >= p.DW || d.y >= p.DH || (p.over > 0 && cnt.n > uint(p.over))) {
        color = texture(app, uv);
        return;
    }
    // What the program drew over the frame (sr_prep.comp: a menu, a notification) is shown as it is: inside
    // its native frame a block that was not one colour in the frame the NPU had, outside it (the bars the
    // upscaled frame covers when it is bigger) anything but black.
    ivec2 r = a - ivec2(p.ox, p.oy);
    if (r.x >= 0 && r.y >= 0 && r.x < p.W * p.A && r.y < p.H * p.A) {
        if (texelFetch(cbcr, r / p.A, 0).b < 0.5) {
            color = texture(app, uv);
            return;
        }
    } else {
        vec4 c = texture(app, uv);
        if (any(greaterThan(c.rgb, vec3(1.5 / 255.0)))) {
            color = c;
            return;
        }
    }
    vec2 n = (vec2(d) + 0.5) / vec2(p.DW, p.DH);   // where in the frame, 0..1
    float Y;
    if (p.DW == p.OW && p.DH == p.OH)
        Y = luma(d);
    else {
        vec2 f = n * vec2(p.OW, p.OH) - 0.5;
        ivec2 i = ivec2(floor(f));
        vec2 t = f - vec2(i);
        Y = mix(mix(luma(i), luma(i + ivec2(1, 0)), t.x), mix(luma(i + ivec2(0, 1)), luma(i + ivec2(1, 1)), t.x), t.y);
    }
    vec2 cc = texture(cbcr, n).rg - 0.5;
    float cr = cc.x, cb = cc.y;
    color = vec4(clamp(vec3(Y + 1.403 * cr, Y - 0.714 * cr - 0.344 * cb, Y + 1.773 * cb), 0.0, 1.0), 1.0);
}
