#version 450

// fullscreen triangle, no VBO needed. gl_VertexIndex in [0,3).
// triangle is bigger than the viewport on purpose, gets clipped to the screen rect.

layout(location = 0) out vec2 vUV;

void main()
{
    vUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vUV * 2.0 - 1.0, 0.0, 1.0);
}
