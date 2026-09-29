#version 440
// Instanced disc/capsule with outline, drawn with a signed distance function.
layout(location = 0) in vec2 corner;  // per vertex
layout(location = 1) in vec4 geom;    // cx, cy, ax, ay (metres; axis = half spine)
layout(location = 2) in vec3 params;  // radius (m), outline (px), marker flag
layout(location = 3) in vec4 fillColor;
layout(location = 4) in vec4 lineColor;

layout(location = 0) out vec2 vLocal; // metres, in the capsule frame
layout(location = 1) flat out vec4 vFill;
layout(location = 2) flat out vec4 vLine;
layout(location = 3) flat out vec4 vShape; // halfLength, radius, outlinePx, marker

layout(std140, binding = 0) uniform Buf {
    mat4 mvp;
    vec2 viewport;
    float pxPerM;
    float dpr;
};

void main()
{
    vec2 c = geom.xy;
    vec2 a = geom.zw;
    float l = length(a);
    vec2 dir = l > 0.0 ? a / l : vec2(1.0, 0.0);
    if (l < 1e-4) l = 0.0; // tiny axes only carry the marker direction
    vec2 n = vec2(-dir.y, dir.x);
    float r = params.x;
    float margin = r + 2.0 / (pxPerM * dpr);
    vec2 local = vec2(corner.x * (l + margin), corner.y * margin);
    vec2 pos = c + dir * local.x + n * local.y;
    vLocal = local;
    vFill = fillColor;
    vLine = lineColor;
    vShape = vec4(l, r, params.y * dpr, params.z);
    gl_Position = mvp * vec4(pos, 0.0, 1.0);
}
