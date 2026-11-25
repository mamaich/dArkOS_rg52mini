#version 450
// Rotate + SR, the upscaled frame where it is its own size (Azahar on the panel, an x4 model on a TV): drawn as a
// rectangle of its own (the pipeline's viewport is the frame's place on the display, rotate.vert's uv 0..1 over
// it), each pixel from the NPU's output (luma, NC1HWC2 C2=16, channel dy*B+dx of a cell) and the native frame's
// colour (Cr,Cb, bilinear as align_corners=False). No test of the rectangle's bounds and no coordinates worked
// out from the program's picture in each pixel, the colour fetched by the varying itself; the bars around it are
// drawn by the plain rotate shader.
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(std430, set = 0, binding = 0) readonly buffer Nout { uint w[]; } nout;
layout(set = 0, binding = 1) uniform mediump sampler2D cbcr;
// The model's values, fixed for the pipeline: as constants the divisions by B become multiplies (the Mali has no
// integer divide). B: output pixels a side of one NPU cell; GH: the NPU grid's height (NC1HWC2: channel c lives
// in the group c>>4, each group GH rows of WSTRIDE cells); OW x OH: the upscaled frame.
layout(constant_id = 0) const int B = 6;
layout(constant_id = 1) const int GH = 120;
layout(constant_id = 2) const int WSTRIDE = 200;
layout(constant_id = 3) const int ZP = -128;
layout(constant_id = 4) const float SCALE = 0.00392157;
layout(constant_id = 5) const int OW = 1200;
layout(constant_id = 6) const int OH = 720;

void main()
{
    ivec2 r = ivec2(uv * vec2(OW, OH));
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
