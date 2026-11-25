#version 450
// sr_nat: the emulator's native frame, W x H, drawn by a fragment pass from the program's picture (which shows it
// enlarged by S, nearest). The texel coordinates are worked out here and reach the fragment shader as varyings,
// so its fetches need no arithmetic: uvA the first pixel of an S x S block, uvB and uvC the points the block is
// checked against (as sr_prep.comp's flat_block).
layout(location = 0) out vec2 uvA;
layout(location = 1) out vec2 uvB;
layout(location = 2) out vec2 uvC;
layout(push_constant) uniform PC { int ox, oy, W, H, S, AW, AH; } p;

void main()
{
    vec2 t = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(t * 2.0 - 1.0, 0.0, 1.0);
    vec2 pc = t * vec2(p.W, p.H);                                    // the native pixel (its centre at +0.5)
    vec2 inv = 1.0 / vec2(p.AW, p.AH);
    uvA = (vec2(p.ox, p.oy) + (pc - 0.5) * float(p.S) + 0.5) * inv;  // the texel (ox + x*S, oy + y*S)
    uvB = uvA + vec2(p.S - 1, max(p.S - 2, 0)) * inv;
    uvC = uvA + vec2(p.S - 1) * inv;
}
