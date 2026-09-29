#version 440
// Instanced anti-aliased line segment. Width is in pixels.
layout(location = 0) in vec2 corner;   // per vertex: (-1|1, -1|1)
layout(location = 1) in vec4 seg;      // per instance: x0, y0, x1, y1 (metres)
layout(location = 2) in float widthPx; // per instance
layout(location = 3) in vec4 color;    // per instance, unorm

layout(location = 0) out vec2 vLocal;  // along/perpendicular, pixels
layout(location = 1) flat out float vLen;
layout(location = 2) flat out float vHalf;
layout(location = 3) flat out vec4 vColor;

layout(std140, binding = 0) uniform Buf {
    mat4 mvp;
    vec2 viewport;
    float pxPerM;
    float dpr;
};

void main()
{
    vec2 p0 = seg.xy;
    vec2 p1 = seg.zw;
    vec2 d = p1 - p0;
    float len = length(d);
    vec2 dir = len > 1e-9 ? d / len : vec2(1.0, 0.0);
    vec2 n = vec2(-dir.y, dir.x);
    float halfPx = 0.5 * widthPx * dpr;
    float hw = (halfPx + 1.5) / (pxPerM * dpr);
    vec2 pos = (corner.x < 0.0 ? p0 - dir * hw : p1 + dir * hw) + n * (corner.y * hw);
    vec2 rel = pos - p0;
    vLocal = vec2(dot(rel, dir), dot(rel, n)) * pxPerM * dpr;
    vLen = len * pxPerM * dpr;
    vHalf = halfPx;
    vColor = color;
    gl_Position = mvp * vec4(pos, 0.0, 1.0);
}
