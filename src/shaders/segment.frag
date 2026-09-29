#version 440
layout(location = 0) in vec2 vLocal;
layout(location = 1) flat in float vLen;
layout(location = 2) flat in float vHalf;
layout(location = 3) flat in vec4 vColor;
layout(location = 0) out vec4 fragColor;

void main()
{
    float dx = max(max(-vLocal.x, vLocal.x - vLen), 0.0);
    float d = length(vec2(dx, vLocal.y));
    // Keep hairlines visible: widths below 1 px fade instead of vanishing.
    float h = max(vHalf, 0.5);
    float cov = clamp(h + 0.5 - d, 0.0, 1.0) * min(vHalf * 2.0, 1.0);
    float a = vColor.a * cov;
    fragColor = vec4(vColor.rgb * a, a);
}
