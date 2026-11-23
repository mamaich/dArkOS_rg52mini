#version 450
// Rotate + SR: inside the area (ox,oy, S*W x S*H) a pixel is built from the NPU's output (luma, NC1HWC2
// C2=16, channel dy*S+dx) and the source frame's colour (Cr,Cb, bilinear as align_corners=False); outside it as rotate.frag.
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(set = 0, binding = 0) uniform sampler2D app;
layout(std430, set = 1, binding = 0) readonly buffer Nout { uint w[]; } nout;
layout(set = 1, binding = 1) uniform sampler2D cbcr;
// B: output pixels a side of one NPU cell (S for ESPCN row5, 2S for the space-to-depth models);
// GH: the NPU grid's height (NC1HWC2: channel c lives in the group c>>4, each group GH rows of wstride cells)
layout(push_constant) uniform PC { int rot, AW, AH, ox, oy, W, H, S, wstride, zp; float scale; int B, GH; } p;

void main()
{
    ivec2 a = ivec2(floor(uv * vec2(p.AW, p.AH)));
    ivec2 r = a - ivec2(p.ox, p.oy);
    if (r.x < 0 || r.y < 0 || r.x >= p.W * p.S || r.y >= p.H * p.S) {
        color = texture(app, uv);
        return;
    }
    int h = r.y / p.S, w = r.x / p.S;
    // a block that was not one colour in the frame the NPU had (sr_prep.comp: a menu, a notification): as it is
    if (texelFetch(cbcr, ivec2(w, h), 0).b < 0.5) {
        color = texture(app, uv);
        return;
    }
    int gh = r.y / p.B, gw = r.x / p.B;
    int c = (r.y - gh * p.B) * p.B + (r.x - gw * p.B);
    uint word = nout.w[(((c >> 4) * p.GH + gh) * p.wstride + gw) * 4 + ((c & 15) >> 2)];
    int q = bitfieldExtract(int(word), (c & 3) * 8, 8);
    float Y = clamp((float(q) - float(p.zp)) * p.scale, 0.0, 1.0);
    vec2 cc = texture(cbcr, (vec2(r) + 0.5) / float(p.S) / vec2(p.W, p.H)).rg - 0.5;
    float cr = cc.x, cb = cc.y;
    color = vec4(clamp(vec3(Y + 1.403 * cr, Y - 0.714 * cr - 0.344 * cb, Y + 1.773 * cb), 0.0, 1.0), 1.0);
}
