# Lighting

## Model

Stylized, not a metallic-roughness BRDF. The material schema carries PBR-shaped
channels (base color, ORM, metallic, roughness, normal, emissive) and the shader
interprets them with a cheap, art-directable model:

- wrapped Lambert diffuse
- normalized Blinn-Phong specular
- hemispheric ambient, replaced by probe irradiance where a volume covers the
  fragment
- additive emission
- exposure and an optional tonemap shoulder applied at the end

There are no directional lights, no area lights, and no image-based specular.

## Light components

Two authored components, both in `engine/include/render`.

| Field | `PointLightComponent` | `SpotLightComponent` |
|---|---|---|
| `Color` | yes | yes |
| `Intensity` | 1.0 | 1.0 |
| `Range` | 10.0 | 10.0 |
| `InnerAngleDegrees` | n/a | 25.0 |
| `OuterAngleDegrees` | n/a | 35.0 |
| `Enabled` | true | true |
| `CastShadows` | false | false |
| `ShadowResolution` | `Medium` | `Medium` |
| `ShadowUpdate` | `OnChange` | `OnChange` |
| `ShadowSoftness` | 1.5 | 1.5 |
| `ShadowBiasScale` | 1.0 | 1.0 |
| `BakeContribution` | `None` | `None` |

Position comes from `WorldTransform`. A spot's direction is the transform's
forward axis. Angles run from the center line to the rim, so the full cone is
twice `OuterAngleDegrees`.

`ShadowResolutionTier` is `Low = 256`, `Medium = 512`, `High = 1024`: the
numeric value **is** the requested atlas tile extent. Point lights carry the
field for schema symmetry but ignore it; every cube face is a fixed 512.

`LightBakeContribution`:

| Value | Runtime forward set | Probe bake | Lightmap bake |
|---|---|---|---|
| `None` | yes | no | no |
| `Indirect` | yes | yes | no |
| `Direct` | **no** | yes | yes |

`Direct` is what removes a static fill light from the per-frame cost entirely:
it is excluded from extraction, takes no cap slot, and casts no runtime shadow.
See [baked-lighting.md](baked-lighting.md).

## Extraction and selection

`LightExtractionSystem::Extract` walks every active registry and, for each
enabled light:

1. reject if `Intensity` or `Range` is non-finite or non-positive
   (`IsUsableForwardLight`)
2. reject if `BakeContribution == Direct`
3. reject if the light's bounding volume misses the camera frustum. Point lights
   test a sphere of `Range`; spot lights test `MakeSpotBoundingSphere`, the
   sphere circumscribing the cone
4. build a `ForwardLightCandidate`, including the shadow view and shadow bounds
   when the light casts

`SelectForwardLights` (`engine/src/render/LightSelection.cpp`) then applies the
one policy shared by the runtime and the editor:

```
score = intensity * clamp(range / max(distance, 1e-4), 0, 1)^2
sort by score descending, ties broken by RenderEntityKey (stable, deterministic)
pack the first min(candidates, 64) into RenderLightSet
each packed light that wants a shadow emits one request, in pack order,
  carrying the packed light index
```

The tie-break on `RenderEntityKey` is what makes selection deterministic: keys
order by registry kind, then persistent `ZoneId` for zone registries or runtime
registry id otherwise, then entity index and generation. Two lights with
identical scores always sort the same way across runs.

`LightExtractionCounts` reports `FrustumCandidates` and `Packed`;
`DroppedAtCap()` is their difference and drives a once-per-episode warning.

## GPU packing

`GpuLight` is 64 bytes and identical in C++ and GLSL:

| Offset | Field | Point | Spot |
|---|---|---|---|
| 0 | `PositionRange` | xyz world position, w range | same |
| 16 | `DirectionCone` | zero | xyz world direction, w cos(outer) |
| 32 | `ColorIntensity` | rgb color, w intensity | same |
| 48 | `Type` | 0 | 1 |
| 52 | `ShadowIndex` | cube slot, or `UINT32_MAX` | atlas slot, or `UINT32_MAX` |
| 56 | `ConeScale` | 0 | `1 / max(cosInner - cosOuter, 1e-4)` |
| 60 | `ConeOffset` | 0 | `-cosOuter * ConeScale` |

`ConeScale` and `ConeOffset` precompute the smoothstep-free cone falloff so the
fragment shader does one multiply-add and a clamp.

`GpuLightType::Directional = 2` exists in the enum. The fragment loop skips any
light with `Type > 1`, so an unimplemented type is inert rather than wrong.

`RenderLightSet` is the frame aggregate: the packed light array, the spot and
point shadow record arrays, the probe volume headers, and the style scalars.
`Reset()` clears the counts only; the arrays keep their previous contents, which
is harmless because nothing past the count is read.

## The fragment path

`engine/shaders/mesh_forward.frag.glsl`, with the terms in
`engine/shaders/lighting.glsli`.

```glsl
baseColor = pushData.BaseColor * texture(BaseColorTexture, uv0)      // if bound
emission  = EmissiveFactor.rgb * max(EmissiveFactor.a, 0) * emissiveTexture

if (MATERIAL_UNLIT) { outColor = ResolveOutput(baseColor.rgb + emission); return; }

orm      = SampleOrm()            // (1,1,0) when unbound: no occlusion, rough, non-metal
normal   = ResolveWorldNormal(geometricNormal)          // TBN mapping when a normal map is bound
hemi     = 0.5 + 0.5 * normal.y
ambient  = mix(AmbientGround, AmbientSky, hemi)
ambient  = SampleProbeAmbient(worldPos, normal, ambient)   // probe volume wins where one covers
ambient  = max(ambient, StyleParams.y)                     // minimum ambient floor
lit      = baseColor.rgb * ambient * clamp(orm.r, 0, 1) * SampleBakedAo()
```

Then the light loop, over `min(LightCount, 64)`:

```glsl
if (light.Type > 1u) continue;                       // unimplemented types are inert

toLight = light.PositionRange.xyz - worldPos;
if (dot(toLight, toLight) >= range*range) continue;  // out of range: contributes exactly zero

visibility = ResolveFilteredShadowVisibility(light, worldPos, geometricNormal);
terms      = EvaluateDirectLight(light, worldPos, normal, viewDir,
                                 diffuseWrap, specularExponent, visibility);
lit += baseColor.rgb * terms.Diffuse  * terms.Radiance;
lit += specularTint  * terms.Specular * terms.Radiance;
```

The range cull is not an approximation: past its range the `r^4` window clamps
to zero, which zeroes `Radiance` and therefore both terms. Culling early is what
stops an unreachable light from paying for the shadow filter's texture taps.
The epsilon matches the window's own denominator so a sub-epsilon range is not
culled early.

`EvaluateDirectLight`:

```glsl
coneAtten = 1
if spot:
    coneAtten = clamp(dot(-L, DirectionCone.xyz) * ConeScale + ConeOffset, 0, 1)
    coneAtten *= coneAtten                                   // squared for a softer rim

ratio     = distance / max(range, 1e-4)
window    = clamp(1 - ratio^4, 0, 1)                         // two multiplies, no pow()
Attenuation = coneAtten * window^2 / (distance^2 + 1e-4)
Radiance    = color * intensity * Attenuation * shadowVisibility

Diffuse  = clamp((dot(N, L) + wrap) / (1 + wrap), 0, 1)
Specular = ((exponent + 8) / 8) * pow(N.H, exponent) * max(SpecularIntensity, 0)
```

`ratio^4` is written as two multiplies on purpose: `pow` with a non-constant
base lowers to `exp2(4 * log2(x))`, a transcendental pair per light per
fragment.

The specular exponent is `exp2(mix(11, 1, roughness))`, so roughness 0 gives
2048 and roughness 1 gives 2. The specular tint is `mix(vec3(1), baseColor,
metallic)`.

Baked static direct is added after the loop, diffuse only, and never
double-counts because the lights that fed it were excluded from the runtime set
at extraction:

```glsl
if (frame.BakedDirectEnabled != 0u)
    lit += baseColor.rgb * SampleBakedDirect();
outColor = vec4(ResolveOutput(lit + emission), baseColor.a);
```

`ResolveOutput` multiplies by exposure and then either applies the shoulder
(`ApplyShoulder`, a per-channel Reinhard-style knee) or clamps to `[0,1]`.

## Style controls

Every one of these is a cvar read once per frame in
`DefaultRenderPipeline::ApplyRendererCVars` and written into `RenderLightSet`.
There are no recompiles and no hardcoded constants behind them.

| cvar | Default | Effect |
|---|---|---|
| `render.ambient.sky_r/g/b` | 0.10 / 0.12 / 0.15 | upper hemisphere ambient |
| `render.ambient.ground_r/g/b` | 0.04 / 0.03 / 0.02 | lower hemisphere ambient |
| `render.style.diffuse_wrap` | 0.25 | Lambert wrap. 0 is hard terminator |
| `render.style.min_ambient` | 0.0 | floor applied after probe sampling |
| `render.exposure` | 1.0 | linear multiply before tonemap |
| `render.tonemap` | true | shoulder on or off |
| `render.tonemap.knee` | 0.8 | where the shoulder starts |
| `render.shadow.darkness` | 1.0 | lerps filtered visibility toward 1.0 |
| `render.shadow.softness` | 1.0 | global multiplier on per-light softness |
| `render.shadow.bias_const` | 4.0 | depth bias constant factor (rebuilds shadow pipelines) |
| `render.shadow.bias_slope` | 2.0 | depth bias slope factor (rebuilds shadow pipelines) |
| `render.baked_direct.enabled` | true | include the baked lightmap term |
| `render.ao.enabled` | true | let baked AO modulate ambient |
| `render.shadow.max_spot` | 8 | live spot slot budget |
| `render.shadow.max_point` | 4 | live point slot budget |
| `render.shadow.max_views_per_frame` | 12 | shared per-frame view clamp |
| `render.shadow.min_invalidated_views_per_frame` | 1 | reserved allotment for invalidated cached slots |

`render.shadow.invalidate` is a command, not a cvar: it marks every rendered
slot for re-render.

## Caps

| Cap | Value | Where |
|---|---|---|
| Forward lights per frame | 64 | `kMaxForwardLights` |
| Spot shadow slots | 8 | `kMaxSpotShadows` |
| Point shadow slots | 4 | `kMaxPointShadows` |
| Active probe volumes | 8 | `kMaxActiveProbeVolumes` |

These are compile-time constants in `engine/include/render/LightGpuTypes.h` and
are mirrored by hand at the top of `engine/shaders/mesh_frame.glsli`. Changing
one means changing both, plus the `static_assert` offsets in
`MeshForwardPass.cpp`. See [shaders.md](shaders.md#keeping-cpu-and-gpu-structs-in-sync).
