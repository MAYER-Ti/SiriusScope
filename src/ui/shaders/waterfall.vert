// Waterfall vertex shader (Qt Quick, QRhi-compatible).
#version 440

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    vec4 params;
};

layout(location = 0) in vec4 vertex;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 v_uv;

void main()
{
    v_uv = texCoord;
    gl_Position = qt_Matrix * vertex;
}
