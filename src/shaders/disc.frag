#version 440
layout(location = 0) in vec2 vLocal;
layout(location = 1) flat in vec4 vFill;
layout(location = 2) flat in vec4 vLine;
layout(location = 3) flat in vec4 vShape;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Buf {
    mat4 mvp;
    vec2 viewport;
    float pxPerM;
    float dpr;
};

void main()
{
    float scale = pxPerM * dpr;
    float l = vShape.x;
    float r = vShape.y;
    float outlinePx = vShape.z;
    vec2 q = vec2(max(abs(vLocal.x) - l, 0.0), vLocal.y);
    float dpx = (length(q) - r) * scale;          // signed distance, pixels
    float inside = clamp(0.5 - dpx, 0.0, 1.0);
    float core = clamp(0.5 - (dpx + outlinePx), 0.0, 1.0);
    vec4 fillP = vec4(vFill.rgb * vFill.a, vFill.a);
    vec4 lineP = vec4(vLine.rgb * vLine.a, vLine.a);
    vec4 col = mix(lineP, fillP, core);
    if (vShape.w > 0.5) {
        // Radius marker so rotation is visible.
        float along = vLocal.x * scale;
        float dm = max(max(-along, along - r * scale), 0.0);
        float md = length(vec2(dm, vLocal.y * scale));
        float mcov = clamp(max(outlinePx * 0.5, 0.6) + 0.5 - md, 0.0, 1.0);
        col = mix(col, lineP, mcov);
    }
    fragColor = col * inside;
}
