#version 450

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec2 inUv0;

layout(push_constant) uniform MeshPush
{
    mat4 World;
    vec4 BaseColor;
} pushData;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 n = normalize(inWorldNormal);
    vec3 lightDir = normalize(vec3(0.35, 0.75, 0.45));
    float lambert = max(dot(n, lightDir), 0.0);
    vec3 color = pushData.BaseColor.rgb * (0.18 + 0.82 * lambert);
    outColor = vec4(color, pushData.BaseColor.a);
}
