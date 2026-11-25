#version 450
// Rotate + SR for a rectangle that is not the upscaled frame's size (fit, fill, a TV): the frame sr_decode.frag
// built, drawn as a rectangle of its own (the pipeline's viewport is its place on the display, rotate.vert's uv
// 0..1 over it): one bilinear sample by the varying itself, no arithmetic in the pixel.
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(set = 0, binding = 0) uniform sampler2D rgb;

void main()
{
    color = vec4(texture(rgb, uv).rgb, 1.0);
}
