#version 450
// The upscaled frame once, for a rectangle that is not its size (fit, fill, a TV): each of its OW x OH pixels from
// the NPU's output (luma, NC1HWC2 C2=16, channel dy*B+dx of a cell) and the native frame's colour (Cr,Cb,
// bilinear as align_corners=False) into an RGBA image, which sr_rect.frag then samples once a pixel, bilinearly,
// by the hardware. Run once a new NPU output, not once a present. A fragment pass into a colour attachment (it
// was a compute pass storing into a storage image: 2.6 ms of the Mali for 1200x720, this 2.1): the image has no
// STORAGE usage, so the driver compresses it and writes it from the tile buffer. rotate.vert with rot 0: uv 0..1.
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(std430, set = 0, binding = 0) readonly buffer Nout { uint w[]; } nout;
layout(set = 0, binding = 1) uniform mediump sampler2D cbcr;
// the model's values: as constants the divisions by B become multiplies
layout(constant_id = 0) const int B = 6;
layout(constant_id = 1) const int GH = 120;
layout(constant_id = 2) const int WSTRIDE = 200;
layout(constant_id = 3) const int ZP = -128;
layout(constant_id = 4) const float SCALE = 0.00392157;

void main()
{
    ivec2 r = ivec2(gl_FragCoord.xy);
    ivec2 g = r / B;
    ivec2 in_cell = r - g * B;
    int c = in_cell.y * B + in_cell.x;
    uint word = nout.w[(((c >> 4) * GH + g.y) * WSTRIDE + g.x) * 4 + ((c & 15) >> 2)];
    int q = bitfieldExtract(int(word), (c & 3) * 8, 8);
    mediump float Y = clamp(float(q - ZP) * SCALE, 0.0, 1.0);
    mediump vec2 cc = texture(cbcr, uv).rg - 0.5;
    mediump float cr = cc.x, cb = cc.y;
    color = vec4(clamp(vec3(Y + 1.403 * cr, Y - 0.714 * cr - 0.344 * cb, Y + 1.773 * cb), 0.0, 1.0), 1.0);
}
