# Instrumentation

Two independent switches:

- **`SENCHA_ENABLE_RENDER_PROFILING`** (CMake option, ON in `dev`, OFF in the
  shipping preset) decides whether the instrumentation bodies are compiled at
  all.
- **`render.profile.mode`** (cvar) decides which tier runs at runtime in a build
  that has them.

## The mode ladder

`RenderProfileMode` (`engine/include/profiling/RenderInstrumentation.h`). Each
mode includes everything below it.

| Mode | Adds |
|---|---|
| `off` | nothing. No timestamp writes, no query resets or readbacks, no history or capture writes, no label commands, no allocations on behalf of profiling |
| `counters` | `RenderStats` per frame plus the history ring, and the CPU scope timings |
| `gpu` | GPU timestamp pools and debug labels |
| `capture` | the capture ring and its export commands |

The mechanism is `ResolveInstrumentationBundle`, a pure function that maps the
active mode plus the candidate stores to a `RenderInstrumentation` whose members
are non-null exactly while their tier is active. `Off` yields an all-null
bundle, which is what makes the off path structurally unable to push a stats
frame or append a capture record. A candidate store that is itself null (no
timestamp support on the device, profiling compiled out) stays null.

The mode is latched once per frame, at the very top of
`FramePhase::ExtractRenderPacket`, before any extraction or recording reads the
bundle. One frame therefore sees exactly one mode.

**Consumers cache the bundle pointer, never its members.** The pointer is stable
for the renderer's life; the members flip with the mode.

## Counters

`RenderStats` (`engine/include/profiling/RenderStats.h`). One producer per
field, written on one thread, reset by the mode latch at the top of extraction.

| Group | Fields | Published by |
|---|---|---|
| Identity | `FrameIndex` | the engine, at push time |
| Forward pass | `VisibleObjects`, `DrawCalls`, `SubmittedTriangles`, `PipelineSwitches`, `MaterialSwitches`, `InstancesDropped` | `MeshRenderFeature::OnDraw` from `MeshForwardPass::DrawStats` |
| Lights | `LightsVisible`, `LightsDroppedAtCap`, `ShadowCastingLights` | `DefaultRenderPipeline::PublishExtractionStats` |
| Shadow pass | `ShadowViewsRendered`, `PointShadowFacesRendered`, `ShadowCasterDraws`, `ShadowCastersTested`, `ShadowCastersVisible`, `ShadowCastersDropped`, `ShadowInstanceRuns` | `ShadowRenderFeature::OnDraw` |
| Shadow residency | `ShadowSlotsHeld`, `ShadowCacheHits`, `ShadowRequestsDenied`, `AtlasTiles1024/512/256`, `PointShadowCubesHeld`, `ShadowTileBytes`, `CasterDiffEvents` | extraction, from `ShadowFrameStats` and per-slot info |
| Baked | `ProbeVolumesResident` | extraction, from `ProbeVolumeSet` |
| Frame services | `ScratchHighWaterBytes`, `ScratchUsedBytes`, `ScratchBytesPerFrame`, `ScratchAllocFailures`, `PassesSkipped` | `Renderer::DrawFrameScheduled` and the passes |

Counter granularity policy: pass-local totals are maintained **unconditionally**
at run granularity (a handful of increments per run, not per instance), and only
the copy into `RenderStats` is gated on the bundle. That keeps the counters
usable as a test seam in an off build without adding per-frame branches to the
draw loop.

Three pairs are designed to be read together, because either number alone lies:

- `ShadowCastersTested` versus `ShadowCastersVisible` says whether culling is
  doing any work. Tested accumulates over views, so a caster tested by six cube
  faces counts six times.
- `ShadowCasterDraws` versus `ShadowInstanceRuns` says whether batching is
  collapsing casters or drawing them one at a time.
- `InstancesDropped` / `ScratchAllocFailures` / `PassesSkipped` are what stop a
  frame that dropped its scene from reading as a cheap frame.

`RenderStatsHistory` is a 512-frame ring with a `Version` counter that must not
advance while the mode is `off`. That is the mechanical proof the off path
writes nothing.

## CPU scopes

`CpuScopeTimings` (`engine/include/profiling/CpuScopeTimings.h`). A closed enum,
for the same reason the GPU set is closed: scope identity is compile-time, so no
per-frame string work exists anywhere.

| `CpuScope` | Measures |
|---|---|
| `Extraction` | walking visible meshes into the queue, across every registry, plus the sort |
| `LightSelection` | gathering candidates, scoring, sorting, packing to the cap |
| `ShadowGather` | gathering casters and diffing them against the previous frame |
| `ShadowResidency` | slot arbitration plus stamping grants onto the lights |
| `ShadowRecord` | recording shadow depth views |
| `ForwardRecord` | recording the forward opaque pass |

A scope that did not run reads `-1`, not `0`: "not measured" and "measured as
free" are different facts, and a mode below `counters` produces the former for
all of them. Values accumulate rather than assign, which is what lets a scope
that runs once per registry or once per view report a frame total.

`CpuScopeTimer` is the RAII form. A null sink makes every operation a no-op,
which is how the off path pays nothing.

## GPU scopes

`GpuScope`, also a closed enum, two timestamps each:

| Scope | Written by |
|---|---|
| `PhaseOffscreen` | `Renderer::DrawFrameScheduled` around the offscreen bucket |
| `PhaseMainColor` | `Renderer::DrawFrameScheduled` around the main color bucket |
| `ShadowViews` | `ShadowRenderFeature::OnDraw` |
| `ForwardOpaque` | `MeshRenderFeature::OnDraw` |

Each scope is also wrapped in a `VK_EXT_debug_utils` label with the same name,
so a RenderDoc or Nsight capture shows the same tree. Pipelines are given object
names at creation (`Forward/StandardLitBack` and friends), which costs nothing
per frame.

Timestamps use `vkCmdWriteTimestamp2` at `ALL_COMMANDS`, so a span measures
everything between the two writes rather than a single stage.

## Debug views

`SENCHA_ENABLE_RENDER_PROFILING` only. Set `render.debug.view` to select a
channel; the value reaches the shader through `MeshFrameUniforms::DebugView` and
switches the forward pass to the debug pipeline family.

| Value | `RenderDebugView` | Shows |
|---|---|---|
| `none` | `None` | normal shading |
| `world_normals` | `WorldNormals` | final shading normal |
| `normal_map` | `NormalMap` | tangent-space sample |
| `normal_delta` | `NormalDelta` | difference between geometric and mapped normal |
| `diffuse` | `Diffuse` | diffuse term only |
| `specular` | `Specular` | specular term only |
| `emission` | `Emission` | emissive term only |
| `roughness` | `Roughness` | resolved roughness |
| `light_complexity` | `LightComplexity` | heat map of lights iterated per fragment |
| `shadow` | `ShadowFiltered` | filtered shadow visibility |
| `shadow_raw` | `ShadowRaw` | unfiltered depth comparison, through the nearest samplers at set 2 bindings 3 and 4 |
| `overdraw` | `Overdraw` | additive, depth-test off, color cleared first |
| `baked_direct` | `BakedDirect` | the lightmap term |
| `lightmap_texels` | `LightmapTexels` | lightmap texel density |
| `baked_ao` | `BakedAo` | the AO plane |

The production shader contains no debug branch. The debug family is a separate
fragment shader compiled only when profiling is enabled.

## Capture

`RenderCapture` (`engine/include/profiling/RenderCapture.h`) is a bounded ring
of `{TimingFrameSample, RenderStats}` records. Ring memory is allocated when
recording first starts, never at engine startup, and all string and JSON work
happens inside the explicit serialize calls.

| Command | Effect |
|---|---|
| `render.capture.start [frames]` | arm recording for N frames, or until stopped when N is 0 or absent. Requires `render.profile.mode capture`, and errors otherwise |
| `render.capture.stop` | disarm |
| `render.capture.write <path>` | serialize. `.csv` writes CSV, anything else writes JSON |
| `render.capture.output` | when non-empty in capture mode, per-frame records are written to this path |

The serialized format is the machine-analysis interface, not a log: a
schema-versioned envelope (`kSchemaVersion = 4`), stable keys, explicit units
(`_ms`, `_bytes`, `_count`).

`SetEnvironment` records device, driver, validation state, and build identity
(the git short SHA is resolved at configure time into the binary). A capture
that cannot name the machine and build it came from cannot be compared against
one from another machine.

`GetVersion()` advances only on `Append`, which proves no capture writes happen
outside capture-mode recording.

## Runtime panels

`SENCHA_ENABLE_DEBUG_UI` builds the ImGui overlay (grave key toggles it), which
hosts `engine/src/debug/RenderStatsPanel.cpp` and `TimingPanel.cpp`. The overlay
ships ON in game builds and is engine-owned in `Engine::Run`; a host opts out
per process with `EngineConfig.Console.UiEnabled = false`, which the editors do.

## Bench harness

`scripts/bench_render_ab.sh` runs a deterministic SceneViewer flythrough N times
and writes one chrome://tracing frame trace per run.

```sh
scripts/bench_render_ab.sh <app-binary> <content-dir> <out-dir> <runs> <frames> <map>
```

What makes a run comparable:

- `sceneviewer.camera.scripted` follows a fixed orbit, so every run renders an
  identical view sequence.
- `app.exit_after_frames` self-terminates the run.
- `frame.trace.output` writes the trace.
- `SENCHA_PRESENT_MODE=IMMEDIATE` and pacing off, so frame times reflect work
  rather than the vsync interval.
- The process is pinned to the performance cores on hybrid CPUs (override with
  `SENCHA_BENCH_CPUS`), because an unpinned run reports the scheduler's choices
  as if they were the renderer's cost.
- Validation is off by default (it costs about 4.8x on CPU render recording);
  `SENCHA_VALIDATION=1` keeps it on for a correctness pass.

Every run is checked before it counts: a run whose assets fell back to defaults,
or whose frames dropped work the scratch could not carry, fails the whole
invocation. Companion analyzers: `scripts/bench_trace_stats.py`,
`scripts/capture_stats.py`, `scripts/scope_scale_stats.py`.

Scene generators for the scaling axes live beside them
(`gen_light_stress_scene.py`, `gen_caster_scale_scene.py`,
`gen_flat_plane_smesh.py`).

## CI guards

| Script | Guards |
|---|---|
| `scripts/check_render_profiling_off.sh [off-build-dir]` | that an OFF build carries no instrumentation. Part A is a source invariant; Part B inspects object files of an OFF-configured build, which is the discriminating artifact (release binaries drop symbols anyway) |
| `scripts/check_render_portability.sh` | that the render layer reaches the OS only through SDL, VMA, and the Vulkan loader |

Both are wired into the CI workflow along with a Windows leg.
