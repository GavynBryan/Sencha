# Sencha Shader Pipeline

This document is the authoritative reference for how shaders are authored,
compiled, packaged, and loaded in Sencha.  It covers the current implementation
and the planned future phases.

---

## Guiding principle

The Vulkan runtime only ever sees SPIR-V.  GLSL is an authoring format.
The compiler runs at build time, not at runtime.  Shipping binaries contain no
GLSL source and no GLSL compiler.

---

## Shader tiers

### Tier 1 — Engine bootstrap shaders

Built-in rendering features (SpriteFeature, debug rendering, fullscreen passes).

- **Source**: `engine/shaders/internal/*.glsl`
- **Compiled**: at build time by `glslc` via `sencha_compile_shader()`
- **Embedded**: SPIR-V words baked into `uint32_t[]` arrays in generated headers
  by `sencha_embed_spirv()`, which invokes `cmake/EmbedSpirv.cmake`
- **Pipeline metadata**: `engine/shaders/internal/*.shader` (JSON), embedded as
  a `constexpr const char*` by `sencha_embed_text()`
- **Loaded by**: `VulkanShaderCache::CreateModuleFromSpirv(array, wordCount)`
  called directly from the feature's `Setup()`.  Zero file I/O.

Generated headers are in `${build}/engine/generated/shaders/` and included as
`#include <shaders/kFooSpv.h>`.  They are never committed to source control.

### Tier 2 — Engine non-bootstrap shaders (future)

Post-process effects, atmospheric passes — engine-owned but not needed before
the asset system initialises.  These will ship in `engine.pak` and be loaded
through the asset system at startup.  Not yet implemented.

### Tier 3 — Game shaders (future)

Project-authored shaders.  They live in the game's `assets/shaders/` tree,
are compiled to `.spv` at build time by the same `sencha_compile_shader()` CMake
function, and are packaged into `game.pak` for distribution.  Not yet
implemented (waiting on the asset system and packer).

---

## Source control rules

| Path | Checked in? |
|---|---|
| `engine/shaders/internal/*.glsl` | **Yes** |
| `engine/shaders/internal/*.shader` | **Yes** |
| `engine/shaders/include/sencha/*.glsli` | **Yes** |
| `build/*/shaders/*.spv` | No (build artifact) |
| `build/*/generated/shaders/*.h` | No (build artifact) |
| `*.vkpc` (VkPipelineCache blobs) | No (user-local, machine-specific) |

---

## Directory layout

```
engine/
  shaders/
    internal/               engine-owned shader sources
      sprite.vert.glsl
      sprite.frag.glsl
      sprite.shader         pipeline metadata (JSON)
    include/
      sencha/               shared GLSL header library
        frame_ubo.glsli     FrameUbo layout (set 0, binding 0)
        bindless.glsli      bindless sampler2D array (set 1, binding 0)

cmake/
  SenchaShaders.cmake       sencha_compile_shader / sencha_embed_spirv / sencha_embed_text
  EmbedSpirv.cmake          cmake -P: .spv binary -> uint32_t[] header
  EmbedText.cmake           cmake -P: text file -> constexpr string header
```

---

## Build pipeline (tier 1, in detail)

```
sprite.vert.glsl
sprite.frag.glsl    ──glslc──►  sprite.vert.glsl.spv
                                sprite.frag.glsl.spv
                        │
                 EmbedSpirv.cmake
                        │
                        ▼
          generated/shaders/kSpriteVertSpv.h   (uint32_t kSpriteVertSpv[])
          generated/shaders/kSpriteFragSpv.h   (uint32_t kSpriteFragSpv[])

sprite.shader  ──EmbedText.cmake──► generated/shaders/kSpriteShaderMetadata.h
                                     (constexpr const char* kSpriteShaderMetadata)
```

Dependency tracking: `glslc -MD -MF` writes a Makefile depfile for each `.spv`
output listing every `#include`d `.glsli` file.  CMake's `DEPFILE` directive
consumes this, so editing any included file only recompiles the affected shaders.

---

## Adding a new engine shader

1. Add `foo.vert.glsl` and/or `foo.frag.glsl` to `engine/shaders/internal/`.
   Use `#include "sencha/frame_ubo.glsli"` etc. for shared declarations.

2. Add `foo.shader` with the pipeline state (see **Metadata format** below).

3. In `engine/CMakeLists.txt`, inside the `if(SENCHA_ENABLE_VULKAN)` block,
   add:

   ```cmake
   sencha_compile_shader(
       SOURCE       "${CMAKE_CURRENT_SOURCE_DIR}/shaders/internal/foo.vert.glsl"
       STAGE        vert
       INCLUDE_DIRS "${_shader_inc}"
       OUTPUT_SPV   _foo_vert_spv
   )
   sencha_embed_spirv(
       SOURCE_SPV  "${_foo_vert_spv}"
       VAR_NAME    kFooVertSpv
       OUT_HEADER  _foo_vert_h
   )
   sencha_embed_text(
       SOURCE     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/internal/foo.shader"
       VAR_NAME   kFooShaderMetadata
       OUT_HEADER _foo_meta_h
   )
   ```

   Add `"${_foo_vert_h}"` and `"${_foo_meta_h}"` to the `DEPENDS` list of
   `sencha_embedded_shaders`.

4. In your feature's `.cpp`:

   ```cpp
   #include <shaders/kFooVertSpv.h>
   #include <shaders/kFooShaderMetadata.h>
   #include <render/backend/vulkan/ShaderMetadata.h>

   // In Setup():
   VertexShader = Shaders->CreateModuleFromSpirv(
       kFooVertSpv, kFooVertSpvWordCount, "foo.vert");

   GraphicsPipelineDesc metaDesc;
   std::string err;
   if (!ParseShaderMetadataToDesc(kFooShaderMetadata, metaDesc, err))
       log.Error("FooFeature: bad shader metadata: {}", err);

   // In BuildPipeline():
   GraphicsPipelineDesc desc = metaDesc;
   desc.VertexShader  = VertexShader;
   desc.FragmentShader = FragmentShader;
   desc.Layout        = PipelineLayout;
   desc.ColorFormats  = { colorFormat };
   // ... add VertexAttributes ...
   ```

---

## Metadata format (.shader)

A `.shader` file is a JSON object.  All fields are optional; defaults are shown.

```jsonc
{
    // Source file names (documentation only -- not used at runtime).
    "vertex":   "foo.vert.glsl",
    "fragment": "foo.frag.glsl",

    // Vertex input bindings.  One entry per VkVertexInputBindingDescription.
    // Stride must match the C++ vertex/instance struct size.
    "vertex_inputs": [
        { "binding": 0, "stride": 48, "rate": "instance" }
        //                                     "vertex" | "instance"
    ],

    // Primitive assembly.
    "topology":     "triangle_list",   // point_list | line_list | line_strip |
                                       // triangle_list | triangle_strip | triangle_fan
    "cull_mode":    "back",            // none | front | back | front_and_back
    "front_face":   "ccw",             // ccw | cw
    "polygon_mode": "fill",            // fill | line | point

    // Depth.
    "depth_test":    false,
    "depth_write":   false,
    "depth_compare": "less_equal",     // never | less | equal | less_equal |
                                       // greater | not_equal | greater_equal | always

    // One blend attachment per color output.
    "blend": [
        {
            "enable":    true,
            "src_color": "src_alpha",              // zero | one | src_color |
            "dst_color": "one_minus_src_alpha",    // one_minus_src_color | dst_color |
            "color_op":  "add",                    // one_minus_dst_color | src_alpha |
            "src_alpha": "one",                    // one_minus_src_alpha | dst_alpha |
            "dst_alpha": "one_minus_src_alpha",    // one_minus_dst_alpha | ...
            "alpha_op":  "add"                     // add | subtract | reverse_subtract | min | max
        }
    ]
}
```

`"comment"` keys are accepted and ignored anywhere in the document.

---

## Runtime contract

### What features call (tier 1)

```cpp
// No file I/O.  No compiler.  Array is in the binary.
ShaderHandle h = Shaders->CreateModuleFromSpirv(
    kMySpv, kMySpvWordCount, "my.vert");
```

### What the future asset system calls (tier 3)

```cpp
// Read a pre-compiled .spv file -- no GLSL compilation.
ShaderHandle h = Shaders->LoadSpirv("game/pbr.vert.spv",
                                     ShaderStage::Vertex, "pbr.vert");
```

### What hot-reload calls (dev builds only)

```cpp
// Compile from GLSL source via glslang, write .spv side-car cache.
// Only available when SENCHA_ENABLE_HOT_RELOAD is defined.
ShaderHandle h = Shaders->LoadFromFile("game/shaders/pbr.vert",
                                        ShaderStage::Vertex);
```

---

## Development workflow

### Default (no hot reload)

Edit a `.glsl` file → run `cmake --build` → rebuild only affected `.spv`,
regenerate only affected `*.h`, recompile only affected `.cpp`.
Incremental; typically a few seconds.

### With hot reload (`-DSENCHA_ENABLE_HOT_RELOAD=ON`)

glslang is linked.  A file watcher (not yet wired -- Phase 5) calls
`VulkanShaderCache::LoadFromFile()` on change, which broadcasts a
`ShaderReloaded` event.  `VulkanPipelineCache` invalidates affected entries
via `VulkanDeletionQueueService`; the next frame uses the new pipeline.

Compiler errors surface in the engine log as `file:line: error:` messages;
the running process continues with the previous shader.

---

## Release / shipping workflow

Set `SENCHA_ENABLE_HOT_RELOAD=OFF` (the default).

- glslang is not fetched, not compiled, not linked.
- `VulkanShaderCache::CompileFromSource`, `LoadFromFile`, `CompileGlsl` are
  compiled out (`#ifdef SENCHA_ENABLE_HOT_RELOAD`).
- Bootstrap shaders load from embedded arrays.  No external files required for
  the render backend to initialise.
- Game shaders (future): packaged into `game.pak` at release build time and
  loaded via `LoadSpirv()` through the asset system.

---

## Backend portability

The layer boundary is at the `.spv` file (or embedded array).  Everything above
it -- GLSL source, `#include` library, `.shader` metadata, `ShaderMetadata.cpp`
parser -- is backend-agnostic in structure even if the descriptor set conventions
are currently Vulkan-specific.

A D3D12 backend would:
- Provide its own `D3D12ShaderCache` with `CreateModuleFromDxil()` and
  `LoadDxil()` entry points matching the same `ShaderHandle` concept.
- Use DXC to compile HLSL (or cross-compile from SPIR-V via SPIRV-Cross) at
  build time, with the same `sencha_compile_shader` wrapper updated for the
  new target.
- Reuse `ParseShaderMetadataToDesc()` and the `.shader` JSON format for
  pipeline state.  The `GraphicsPipelineDesc` abstraction would need a
  backend-agnostic counterpart, but the metadata layer remains unchanged.

---

## Planned phases

| Phase | Description | Status |
|---|---|---|
| 1 | Build-time SPIR-V, embedded arrays, glslang gated | **Done** |
| 2 | Pipeline metadata in `.shader`, metadata-driven `BuildPipeline` | **Done** |
| 3 | Build-time SPIR-V reflection (spirv-reflect): validate binding / vertex layout agreement between SPIR-V and `.shader` | Planned |
| 4 | Game shader authoring: `sencha_compile_shader` for project shaders, `.spv` in build dir, `LoadSpirv` path | Planned |
| 5 | Hot reload: file watcher → `LoadFromFile` → `ShaderReloaded` event → pipeline cache invalidation | Planned |
| 6 | Packaging: `game.pak` / `engine.pak`, pak-backed `LoadSpirv` | Planned |
