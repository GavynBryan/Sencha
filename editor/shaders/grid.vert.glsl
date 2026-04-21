#version 450

layout(push_constant) uniform GridPC {
    mat4 viewProj;
    vec3 gridCenter;
    float halfExtent;
    vec3 axisU;
    float spacing;
    vec3 axisV;
    float subdivSpacing;
    vec3 cameraPos;
    float fadeEnd;
} pc;

layout(location = 0) out vec3 fragWorldPos;

void main()
{
    // Two triangles covering a large quad on the grid plane.
    // Corners: (-1,-1),(1,-1),(1,1),(-1,-1),(1,1),(-1,1)
    int vi = gl_VertexIndex;
    float s = (vi == 1 || vi == 2 || vi == 4) ?  1.0 : -1.0;
    float t = (vi == 2 || vi == 4 || vi == 5) ?  1.0 : -1.0;

    vec3 worldPos = pc.gridCenter
        + pc.axisU * (s * pc.halfExtent)
        + pc.axisV * (t * pc.halfExtent);

    fragWorldPos = worldPos;
    gl_Position  = pc.viewProj * vec4(worldPos, 1.0);
}
