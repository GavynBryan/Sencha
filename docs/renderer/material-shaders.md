# Material shaders

A game gives a material its own look by registering a fragment shader, not by
writing render code. Every material that names the shader draws with it instead of
the built-in shading, through the same pipelines, sort and passes.

## Registering a shader

`Game::OnRegisterMaterialShaders(MaterialShaderCatalog&)` runs at startup, before
content loads, and Kyusu calls it when it loads a project's module. A
`MaterialShaderDesc` is a name and SPIR-V. Registration refuses, and the engine
logs, a duplicate name, words that are not SPIR-V, a shader that reads an input the
mesh vertex stage does not write, and push constants past the mesh block.

Compile with `sencha_compile_shader`, as for [post effects](post-effects.md):

```glsl
#version 450
#extension GL_GOOGLE_include_directive : require
#include "mesh_view.glsli"
#include "mesh_material.glsli"

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 base = SampleBaseColor();
    float seconds = frame.ViewPositionTime.w;
    outColor = MeshOutput(base.rgb, pushData.Params[0].rgb * seconds, base.a);
}
```

## The interface

| What | Where |
|---|---|
| World position, normal, tangent, UV0 | the inputs `mesh_material.glsli` declares |
| Camera position and time | `frame.ViewPositionTime`: simulation seconds in a game, which stand still while it is paused; the editor's clock in a viewport |
| The material's factors and four texture slots | `pushData`, with `SampleBaseColor`, `ResolveEmission` and the other samplers |
| Two `vec4`s that mean whatever the shader says | `pushData.Params`, from the material's `shader_params` |
| Lit, masked and blended variants | the `MATERIAL_UNLIT`, `MATERIAL_ALPHA_MASK` and `MATERIAL_BLEND` constants |

Write the colour through `MeshOutput`: it premultiplies when the material blends, so
emission is not dimmed by coverage. The vertex stage is the engine's, and the depth,
shadow, outline and debug passes draw a material shader's surfaces with their own
shaders.

## Authoring

```json
{ "version": 2, "shader": "game.surface", "shader_params": [[0.2, 0.6, 1.0, 4.0], [1, 0, 0, 0]] }
```

A material resolves the name when it loads. A name nothing registered logs a warning
and draws with the built-in shading, as does a shader whose pipelines fail to build.

## Not implemented

Shader hot reload: rebuild the game and reload the module. Editing a material's
values reloads live.
