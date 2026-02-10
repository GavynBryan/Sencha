#pragma once

//=============================================================================
// Shaders
//
// Embedded GLSL shader sources for the maze renderer. Uses OpenGL 3.3
// core profile. Vertex shader transforms geometry by a model-view-
// projection matrix. Fragment shader applies per-face color with simple
// directional lighting.
//=============================================================================
namespace Shaders
{

inline constexpr const char* Vertex = R"glsl(
#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;

uniform mat4 uMVP;

out vec3 vNormal;
out vec3 vColor;

void main()
{
    gl_Position = uMVP * vec4(aPosition, 1.0);
    vNormal = aNormal;
    vColor = aColor;
}
)glsl";

inline constexpr const char* Fragment = R"glsl(
#version 330 core

in vec3 vNormal;
in vec3 vColor;

out vec4 FragColor;

void main()
{
    vec3 lightDir = normalize(vec3(0.3, 1.0, 0.5));
    float ambient = 0.25;
    float diffuse = max(dot(normalize(vNormal), lightDir), 0.0) * 0.75;
    float lighting = ambient + diffuse;
    FragColor = vec4(vColor * lighting, 1.0);
}
)glsl";

} // namespace Shaders
