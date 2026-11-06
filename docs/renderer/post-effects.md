# Post effects

A game adds a look by registering a shader, not by writing render code. It never
implements `IRenderFeature`, opens a rendering scope, or sees a Vulkan handle.

## Registering an effect

`Game::OnRegisterPostEffects(PostEffectCatalog&)` runs at startup. A `PostEffectDesc` is a
name, SPIR-V, a domain, and an order; a duplicate name or a duplicate (domain, order) is
refused, because effects compose in that order, and the engine logs each refusal with its
reason. The catalog keeps its own copy of the SPIR-V, so a module's data is not read after
registration, and starts each `Engine::Run` holding only the engine's effects. Every effect
compiles when the renderer sets up rather than on the frame it first has weight; one that
fails to compile is skipped, and not tried again.

When a display effect runs, the frame's outlines are drawn after the transform and before
the display effects, so a fade covers them too. See [outlines.md](outlines.md).

Compile the shader with `sencha_compile_shader` and embed it with `sencha_embed_spirv`,
passing `SENCHA_SHADER_INCLUDE_DIR` as an include directory. Both ship in the SDK.

```cmake
sencha_compile_shader(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/my_effect.frag.glsl" STAGE frag
                      INCLUDE_DIRS "${SENCHA_SHADER_INCLUDE_DIR}" OUTPUT_SPV _spv)
sencha_embed_spirv(SOURCE_SPV "${_spv}" VAR_NAME kMyEffectSpv OUT_HEADER _header)
```

```glsl
#version 450
#include "post_effect.glsli"

void main()
{
    outColor = SampleScene(vUv + post.params[0].xy * post.weight);
}
```

`post_effect.glsli` is the whole interface: the bindless image array and a 96-byte push
block holding the source image, the texel size, wall seconds that keep advancing while the
simulation is paused, the weight, and four `vec4` params. `depthIndex` and `maskIndex` read
`0xFFFFFFFF` until something publishes them. An effect that needs a descriptor of its own,
or more than the 128 bytes every device guarantees, is a design conversation rather than a
bigger block.

The engine ships `sencha.fade`, a display-domain fade to `params[0].rgb`.

## Domains

| Domain | Sees | Runs |
|---|---|---|
| `Scene` | linear scene-referred light | before the output transform |
| `Display` | linear display-referred light, in [0,1] | after it |

With no display effect the output pass applies the transform as usual. With one, the chain
runs the transform into a plane first, so one display effect costs its own pass plus that
transform pass, and the present draw stays. Further effects in either domain cost one pass
each. Nothing active costs nothing: a contribution at weight 0 is not scheduled, the chain
records nothing, and its two planes are released after a quiet interval.

## Contributions

`SetPostEffect(world, effect, contribution)` holds a contribution in the World's
[store](presentation-contributions.md). One instance of an effect type runs per view, won
by the highest `Order`.

- **Audience.** `AudienceSubject` is one camera subject, matched by `CameraViewIsOwnedBy`
  as camera shakes are. None means every view of this World. A view sees only its own
  World's contributions, so Kyusu's edit viewports see none.
- **Weight.** The owner drives it every frame, on whatever clock suits. The simulation
  clock freezes while paused, so an effect that plays during a pause drives it from
  `FrameUpdateContext::WallDeltaSeconds`.
- **Lifetime.** `Held` ends with its holder entity. `Session` survives pause, level unload,
  and pawn destruction, and ends only by an explicit clear, by `ClearPostEffectsOf(writer)`,
  or when `RemainingWallSeconds` runs out.
