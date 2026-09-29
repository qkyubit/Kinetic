#version 440
layout(location = 0) in vec2 pos;   // metres
layout(location = 1) in vec4 color; // unorm
layout(location = 0) flat out vec4 vColor;

layout(std140, binding = 0) uniform Buf {
    mat4 mvp;
    vec2 viewport;
    float pxPerM;
    float dpr;
};

void main()
{
    vColor = color;
    gl_Position = mvp * vec4(pos, 0.0, 1.0);
}
