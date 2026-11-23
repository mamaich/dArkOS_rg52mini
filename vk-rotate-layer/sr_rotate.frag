#version 450
// Rotate + SR: inside the rectangle (dx,dy, DW x DH) a pixel is built from the NPU's output (luma, NC1HWC2
// C2=16, channel dy*B+dx of a cell; the upscaled frame is OW x OH, resampled bilinearly when the rectangle
// is another size) and the native frame's colour (Cr,Cb, bilinear as align_corners=False); outside it as
// rotate.frag. The program's own native frame is the A*W x A*H area at ox,oy. A frame with a menu over it is
// not drawn by this pass at all: npusr.inc.c picks the plain rotate pass from prep's count, once a frame.
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(set = 0, binding = 0) uniform sampler2D app;
layout(std430, set = 1, binding = 0) readonly buffer Nout { uint w[]; } nout;
layout(set = 1, binding = 1) uniform mediump sampler2D cbcr;
// the rotate pass's push constants (rotate.vert reads rot); the model's own values below
layout(push_constant) uniform PC { int rot, AW, AH, ox, oy, W, H, S, wstride, zp; float scale; int B, GH, A, dx, dy, DW, DH, OW, OH, moved; } p;
// The model's and the rectangle's values, fixed for the pipeline (npusr.inc.c, sr_swap_build): as constants
// the divisions by B become multiplies (the Mali has no integer divide; two a pixel cost ~10 instructions
// each) and the branches that do not apply go. B: output pixels a side of one NPU cell (S for ESPCN row5, 2S
// for the space-to-depth models, S*K for cells); GH: the NPU grid's height (NC1HWC2: channel c lives in the
// group c>>4, each group GH rows of WSTRIDE cells); DIRECT: the rectangle is the upscaled frame's size;
// MOVED: the rectangle is not the program's native frame (fit, fill).
layout(constant_id = 0) const int B = 6;
layout(constant_id = 1) const int GH = 120;
layout(constant_id = 2) const int WSTRIDE = 200;
layout(constant_id = 3) const int ZP = -128;
layout(constant_id = 4) const float SCALE = 0.00392157;
layout(constant_id = 5) const int OW = 1200;
layout(constant_id = 6) const int OH = 720;
layout(constant_id = 7) const bool DIRECT = true;
layout(constant_id = 8) const bool MOVED = false;

// a pixel of the upscaled frame (clamped to it)
mediump float luma(ivec2 r)
{
    r = clamp(r, ivec2(0), ivec2(OW - 1, OH - 1));
    ivec2 g = r / B;                       // a constant divisor: a multiply and a shift
    ivec2 in_cell = r - g * B;
    int c = in_cell.y * B + in_cell.x;
    uint word = nout.w[(((c >> 4) * GH + g.y) * WSTRIDE + g.x) * 4 + ((c & 15) >> 2)];
    int q = bitfieldExtract(int(word), (c & 3) * 8, 8);
    return clamp(float(q - ZP) * SCALE, 0.0, 1.0);
}

void main()
{
    ivec2 a = ivec2(floor(uv * vec2(p.AW, p.AH)));
    ivec2 d = a - ivec2(p.dx, p.dy);
    if (d.x < 0 || d.y < 0 || d.x >= p.DW || d.y >= p.DH) {
        color = texture(app, uv);
        return;
    }
    // Moved (fit, fill), the upscaled frame covers the program's bars: anything but black there (PPSSPP's
    // notifications at the top) is shown as drawn. A notification over the frame itself is upscaled with it.
    if (MOVED) {
        ivec2 r = a - ivec2(p.ox, p.oy);
        if (r.x < 0 || r.y < 0 || r.x >= p.W * p.A || r.y >= p.H * p.A) {
            vec4 c = texture(app, uv);
            if (any(greaterThan(c.rgb, vec3(1.5 / 255.0)))) {
                color = c;
                return;
            }
        }
    }
    vec2 n = (vec2(d) + 0.5) / vec2(p.DW, p.DH);   // where in the frame, 0..1
    mediump float Y;
    if (DIRECT)
        Y = luma(d);
    else {
        vec2 f = n * vec2(OW, OH) - 0.5;
        ivec2 i = ivec2(floor(f));
        mediump vec2 t = f - vec2(i);
        Y = mix(mix(luma(i), luma(i + ivec2(1, 0)), t.x), mix(luma(i + ivec2(0, 1)), luma(i + ivec2(1, 1)), t.x), t.y);
    }
    mediump vec2 cc = texture(cbcr, n).rg - 0.5;
    mediump float cr = cc.x, cb = cc.y;
    color = vec4(clamp(vec3(Y + 1.403 * cr, Y - 0.714 * cr - 0.344 * cb, Y + 1.773 * cb), 0.0, 1.0), 1.0);
}
