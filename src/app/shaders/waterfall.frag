#version 440

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    vec4 params;
};

layout(binding = 1) uniform sampler2D source;

vec3 colormap(float t)
{
    float r = smoothstep(0.0, 0.4, t) * 0.9 + smoothstep(0.7, 1.0, t) * 0.1;
    float g = smoothstep(0.1, 0.7, t);
    float b = 1.0 - smoothstep(0.0, 0.5, t);
    return vec3(r, g, b);
}

void main()
{
    float rowOffset = params.y;
    vec2 uv = vec2(vTexCoord.x, fract(vTexCoord.y + rowOffset));
    float value = texture(source, uv).r;
    vec3 color = colormap(value);
    fragColor = vec4(color, params.x);
}
