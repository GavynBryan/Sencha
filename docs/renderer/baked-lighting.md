# Baked Lighting

Three baked products, all zone-scoped, all streamed with the zone that owns
them, all sampled by the same forward fragment shader.

| Product | Storage | Vulkan format | Sampled through | Modulates |
|---|---|---|---|---|
| Static direct diffuse | per-zone lightmap atlas | `E5B9G9R9_UFLOAT_PACK32` (RGB9E5) | bindless set 1, by push-constant index | added after the light loop, diffuse only |
| Ambient occlusion | per-zone plane sharing the atlas parameterization | `R8_UNORM` | bindless set 1, by push-constant index | the ambient term only |
| Irradiance | per-zone L1 SH probe volumes | `R16G16B16A16_SFLOAT` 3D, three per volume | set 2 binding 2, by frame-UBO header | replaces hemispheric ambient where a volume covers |

The bake itself lives in the editor and the cook layer
(`engine/src/assets/cook`, `editor/kyusu/src/document`) and is out of scope
here. This document covers what the renderer consumes.

## Baked static direct

A light authored `LightBakeContribution::Direct` has its diffuse contribution
baked into the zone's lightmap atlas and is **removed from the runtime forward
set** at extraction. It takes no cap slot, costs nothing per frame, and casts no
runtime shadow. That exclusion is exactly what prevents double counting: the
shader adds the baked term unconditionally, and the lights that fed it are not
in the loop.

### Addressing

`StaticMeshVertex` carries a `unorm16` lightmap UV pair, read as
`VK_FORMAT_R16G16_UNORM` at location 8. Two authoring cases share one code path:

- **Cooked cell meshes** store absolute atlas texel-center coordinates, and
  their instance uses the identity `LightmapScaleBias` of `(1, 1, 0, 0)`.
- **Instanceable meshes** store a `[0,1]` sheet, and the cook assigns each
  placement a rect; `StaticMeshComponent::LightmapScaleBias` remaps it.

The vertex shader computes `outLightmapUv = inLightmapUv * scaleBias.xy +
scaleBias.zw`, so the fragment shader sees atlas coordinates either way.

Zero is a safe lightmap UV on an unbaked mesh: with the identity remap it lands
on the atlas's reserved black border texel, and items with no atlas skip the
term entirely.

### Resolution and per-draw plumbing

`ZoneLightmapComponent` holds the zone's atlas texture and its AO plane
(`TextureHandle` each, either may be invalid). The level cook emits one per
cooked zone scene that has baked lights.

`RenderExtractionSystem::Extract` resolves both to bindless indices **once per
registry pass**, then stamps them on every item that registry emits. Both
indices are part of the run-merge identity, because they are uniform-per-draw
push constants: the same mesh resident in two zones must not share a run.

### Shader

```glsl
vec3 SampleBakedDirect()
{
    if (pushData.LightmapTextureIndex == 0xFFFFFFFFu) return vec3(0.0);
    return texture(BindlessTextures[pushData.LightmapTextureIndex], inLightmapUv).rgb;
}
```

One bilinear fetch. RGB9E5 decodes per texel **before** filtering, so the
filtered result is linear in radiance, which is why the format was chosen over
RGBM. `render.baked_direct.enabled` gates the addition at the frame level.

## Baked ambient occlusion

An `R8_UNORM` plane sharing the lightmap atlas's charts, gutters, dilation, and
UVs, baked by cosine-weighted hemisphere rays against the same occlusion BVH
(neighbor halo included) at one estimate per luxel.

```glsl
float SampleBakedAo()
{
    if (pushData.AoTextureIndex == 0xFFFFFFFFu || frame.BakedAoEnabled == 0u) return 1.0;
    return texture(BindlessTextures[pushData.AoTextureIndex], inLightmapUv).r;
}
```

**AO modulates the ambient term only.** It joins the material's own occlusion
channel (`orm.r`) on `baseColor * ambient` and never touches direct light, baked
direct, or emission. AO is not a substitute for a shadow map, and it must never
contain sunlight; when directional lights land, that rule is what keeps the two
mechanisms separable.

`render.ao.enabled` gates it at the frame level.

## Irradiance probe volumes

### Data

A zone cooks a `.sprobe` file (`engine/include/assets/probes/ProbeVolumeFormat.h`,
magic `SPRB`, version 1) beside its scene: for `<dir>/<stem>.cooked.json` the
cook writes `<dir>/<stem>/probes.sprobe`. A missing file means the zone
authored no volumes, which is not an error.

Each volume record carries a `GridTransform3d` lattice, a priority, a stable
cook-order index, and three fp16 channel planes (R, G, B) of
`PointCount() * 4` coefficients in `(c1x, c1y, c1z, c0)` order and Grid3d point
order. That is exactly one 3D-texture upload per channel with no repacking at
load. An optional validity bit plane marks probes that were dilated rather than
baked; the runtime ignores it and the editor overlay tints with it.

### Residency

`ProbeVolumeSet` (`engine/src/render/ProbeVolumeSet.cpp`) creates three RGBA16F
3D images per volume, uploads the planes verbatim, and points a set 2 binding 2
slot triple at them. Slots are assigned at load and stay stable until the owning
zone releases them. Volumes past `kMaxActiveProbeVolumes = 8` are denied with a
warning and render with the hemispheric fallback.

Lifetime is tied to the registry, not to any host hook: `AttachZoneProbes`
installs a `ZoneProbeResidency` resource whose destructor calls `ReleaseZone`.
Zone destruction (streaming unload or shutdown) therefore releases the volumes
automatically. Image destruction defers through the deletion queue, and
registries die before graphics services.

Threading: `AddZoneVolumes` and `ReleaseZone` run on the owner thread at the
async drain point; extraction runs on the same thread later in the same frame.
There is no concurrent access. Binding 2 is `UPDATE_AFTER_BIND` precisely so a
slot swap is legal while frames holding the set are still in flight.

`AppendActive` copies the resident headers of every listed registry into
`RenderLightSet::ProbeVolumes` each frame, bounded by the light set's own cap.

### GPU header

`GpuProbeVolume`, 64 bytes, built by `MakeGpuProbeVolume`:

| Field | Contents |
|---|---|
| `ScaleChannelBase` | xyz world-to-uvw scale, w the first binding-2 array element for this volume (`slot * 3`) |
| `BiasPriority` | xyz world-to-uvw bias, w the authored priority |
| `UvwMinVolume` | xyz lattice minimum (half a cell), w the world volume |
| `UvwMaxStableIndex` | xyz lattice maximum, w the cook-order stable index |

### Sampling

`engine/shaders/probe_sampling.glsli`:

```glsl
for each resident volume:
    uvw = worldPos * ScaleChannelBase.xyz + BiasPriority.xyz
    skip if uvw outside [UvwMin, UvwMax]
    keep the best by: highest priority, then smallest world volume,
                      then lowest stable index
if none covered: return the caller's hemispheric ambient

base  = uint(best.ScaleChannelBase.w)
shR/G/B = texture(ProbeVolumes[nonuniformEXT(base + 0/1/2)], uvw)
eval  = vec4(normal * PROBE_SH_C1, PROBE_SH_C0)
return max(vec3(dot(shR, eval), dot(shG, eval), dot(shB, eval)), vec3(0))
```

Points worth knowing:

- The precedence chain is total and stable, so the winning volume cannot flicker
  between frames or reloads.
- Coverage is bounded by **texel centers**, not the lattice box, so the outer
  half cell falls back to hemispheric ambient rather than sampling a
  sampler-clamped edge.
- Volume selection is per fragment, so the sampler index is not dynamically
  uniform within a draw. That is why the fragment shader requires
  `GL_EXT_nonuniform_qualifier` and the device floor requires
  `shaderSampledImageArrayNonUniformIndexing`.
- Validity is resolved at bake time by dilation, so runtime sampling is plain
  hardware trilinear with no per-tap validity logic.
- `PROBE_SH_C0 = 0.2820948` and `PROBE_SH_C1 = 0.4886025 * (2/3)` fold the
  Lambert cosine-lobe convolution and the `1/pi` diffuse factor into the basis
  constants. They must match the CPU reference `EvaluateProbeShL1` in
  `assets/cook/ProbeBake` in formulation, not just numerically.

### Measured cost

Probe sampling was measured at 0.04 to 0.06 ms per frame on an RTX 4060 Laptop
against a 0.3 ms budget. Evidence lives in
`docs/plans/evidence/probe-sampling-cost/`.

## Interaction summary

The ambient path, in order:

```
hemi ambient (AmbientGround..AmbientSky by normal.y)
  -> replaced by probe irradiance where a volume covers the fragment
  -> raised to render.style.min_ambient
  -> multiplied by material occlusion (orm.r)
  -> multiplied by baked AO
```

The direct path is untouched by any of that, and baked direct is added after
both.
