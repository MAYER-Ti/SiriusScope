// Waterfall fragment shader (simple colormap).
#version 440

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    vec4 params;
};

layout(binding = 1) uniform sampler2D waterfallTex;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

vec3 colormap(float t)
{
    t = clamp(t, 0.0, 1.0);
    vec3 c0 = vec3(0.02, 0.03, 0.08);
    vec3 c1 = vec3(0.00, 0.22, 0.60);
    vec3 c2 = vec3(0.00, 0.75, 0.80);
    vec3 c3 = vec3(0.95, 0.85, 0.30);
    vec3 c4 = vec3(1.00, 1.00, 1.00);

    if (t < 0.25) {
        return mix(c0, c1, t / 0.25);
    } else if (t < 0.55) {
        return mix(c1, c2, (t - 0.25) / 0.30);
    } else if (t < 0.85) {
        return mix(c2, c3, (t - 0.55) / 0.30);
    }
    return mix(c3, c4, (t - 0.85) / 0.15);
}

void main()
{
    float opacity = params.x;
    float rowOffset = params.y;

    vec2 uv = vec2(v_uv.x, fract(v_uv.y + rowOffset));
    float value = texture(waterfallTex, uv).r;
    vec3 color = colormap(value);
    fragColor = vec4(color, opacity);
}
