#version 450
// Moved (fit, fill), the upscaled frame covers the program's bars: anything but black there (PPSSPP's
// notifications at the top) is shown as drawn. Drawn after the frame, over the part of it outside the program's
// own frame: black is discarded and the frame stays. A notification over the frame itself is upscaled with it.
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(set = 0, binding = 0) uniform sampler2D app;

void main()
{
    vec4 c = texture(app, uv);
    if (!any(greaterThan(c.rgb, vec3(1.5 / 255.0)))) discard;
    color = c;
}
