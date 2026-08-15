# Rendering and Graphics Codacy Findings

Status: triaged, not started. Every finding below was read against source; the
verdicts are recorded so they do not get re-litigated.

Sencha was connected to Codacy on 2026-08-15. The first analysis ran against
`e9f9a32d` using Codacy's unmodified default coding standard and produced 626
findings. Nobody selected those patterns for this codebase, so the set is a raw
tool baseline rather than a reviewed defect list.

The triage below was performed against `e9f9a32d` and re-checked at `d3e53e1a`.
The commits between the two touch only `net/`, `participant/`, `app/`,
`template/`, and `test/`, so no finding, verdict, or line reference here is
affected by them.

Filtering to the rendering and graphics domain gives 114 findings:

| Layer | Count | Paths |
| --- | --- | --- |
| Vulkan backend | 26 | `engine/src/graphics/vulkan/` |
| Render layer | 29 | `engine/src/render/` |
| Pipeline and diagnostics | 4 | `DefaultRenderPipeline`, `RenderStatsPanel`, `RenderCapture`, `check_render_portability.sh` |
| Editor render | 17 | `editor/kyusu/src/render/`, Shudei material preview |
| Graphics asset pipeline | 38 | mesh, texture, lightmap, and probe cook and loaders |

By pattern: 79 over-long methods, 6 over-size files, 8 over-parameter
signatures, 16 Flawfinder `memcpy` warnings, 3 Cppcheck ErrorProne and scope
hits, 1 ShellCheck.

The overwhelming majority are false positives or benign truths. The genuine
content is small: two defects in the cooked-texture path, one service-boundary
validation gap, and four cases of duplication that the line-count patterns
surfaced as a side effect. Two of the "always true" findings are wrong in a way
that matters — acting on them introduces an unsigned underflow — and one
"reduce scope" finding would cost a multi-megabyte allocation per bake round if
applied.

The goal is therefore not a zero issue count. It is to fix what is broken,
remove the duplication the tool correctly pointed at, and retune the analyzer so
its future output is signal. Separately, the repository already carries a
render-scoped [`.clang-tidy`](../../.clang-tidy) that runs from neither CMake nor
CI; that config would produce far better findings than Lizard line counts, and
section 5 turns it on.

## 1. Defects

### 1.1 The `.stex` bounds check integer-overflows

[`TextureLoader.cpp`](../../engine/src/assets/texture/TextureLoader.cpp) line 40
guards the pixel-data copy with:

```cpp
if (uint64_t(header.PixelDataOffset) + header.PixelDataSize > bytes.size())
```

`PixelDataOffset` is `uint32_t` and `PixelDataSize` is `uint64_t`
([`TextureFormat.h`](../../engine/include/assets/texture/TextureFormat.h)), both
fully file-controlled. A crafted `.stex` with `PixelDataSize` near `UINT64_MAX`
wraps the sum and passes the check.

This is not memory-unsafe: `texture.Blob.resize(header.PixelDataSize)` runs
first and throws for any value large enough to have wrapped. The impact is an
uncaught exception on a malformed cooked asset.

Reformulate as a subtraction that cannot wrap, after the existing offset check:
`header.PixelDataSize > bytes.size() - header.PixelDataOffset`.

Regression coverage goes in `test/core/TextureAssetTests.cpp`: a stream with a
wrapping `PixelDataSize` is rejected with the existing
`"stex: pixel data out of range"` diagnostic rather than throwing.

### 1.2 `Image` carries an unenforced size invariant

[`TextureCook.cpp`](../../engine/src/assets/cook/TextureCook.cpp) line 219 copies
`texture.Mips[0].ByteSize` — which is `Width * Height * 4`, derived purely from
the declared dimensions — out of `image.Pixels`. The only guard is
`image.IsValid()`, and [`Image`](../../engine/include/render/Image.h) defines
that as `!Pixels.empty() && Width > 0 && Height > 0`. It never checks that
`Pixels.size()` agrees with the dimensions.

This holds today only because every in-tree producer is `LoadImageFromFile` or
`LoadImageFromMemory` ([`ImageLoader.cpp`](../../engine/src/render/ImageLoader.cpp)),
which assign exactly `w * h * 4` from stb_image. It is a latent over-read waiting
on the first hand-constructed `Image`: a procedural texture, a test fixture, a
future decoder.

The invariant belongs to `Image`, not to the copy site, so the check moves into
`Image::IsValid()`. While there, `Image::ByteSize()` computes `Width * Height * 4`
in `uint32_t` arithmetic and overflows past roughly 32k squared; it should widen
to `uint64_t`. `ByteSize()` is reachable through an installed header, so check
`TextureCache` and the `VulkanImageService` callers for narrowing at the
assignment before widening.

### 1.3 `UploadMips` does not bound regions at the service boundary

[`VulkanImageService.cpp`](../../engine/src/graphics/vulkan/VulkanImageService.cpp)
line 448 validates only `region.Offset >= size` for each region, never that the
region *ends* inside the staging buffer, and `MipUploadRegion` carries no byte
size at all. The `vkCmdCopyBufferToImage` extents come from `region.Width` and
`region.Height`, so a region near the end of the blob makes the GPU read past
the staging allocation.

This is safe today only because `TextureCache::UploadGpuImage` runs
`ValidateTextureData` first, which proves the mip table exactly tiles the blob.
`UploadMips` is a public service API that a future caller can reach without the
validator, so the end bound belongs at the boundary. It folds into item 2.1,
which rewrites this function's staging path anyway.

## 2. Duplication

These four surfaced as line-count findings, but length is not the problem. Each
is a place where a change has to be made twice.

### 2.1 The two Vulkan upload paths share about 55 copy-pasted lines

`VulkanImageService::Upload` (line 301) and `UploadMips` (line 417) duplicate
verbatim:

- staging buffer creation, `memcpy`, flush, `UploadCtx->Begin()`, and the
  failure cleanup (lines 325-347 against 456-478),
- the `UNDEFINED` to `TRANSFER_DST` whole-chain barrier (358-372 against 489-503),
- the `TRANSFER_DST` to `SHADER_READ_ONLY` barrier (396-411 against 525-539).

Both also hardcode `AspectMask` to `VK_IMAGE_ASPECT_COLOR_BIT` rather than
reading `entry->AspectMask`. That is harmless while only color images take the
upload path, and worth correcting once there is a shared helper to correct it
in.

An anonymous-namespace RAII `StagingBuffer` plus a
`TransitionWholeChain(cmd, entry, from, to)` helper, both file-local in
`VulkanImageService.cpp`. These are private details of one service with no
second consumer; a separate translation unit would be a seam with no boundary
behind it. The result halves both functions and makes the actual difference
between them visible — one `VkBufferImageCopy` against N.

Nothing under `test/` references `VulkanImageService`, and there is no Vulkan
device harness in this repository. This change is blind and is verified by
running the app.

### 2.2 The glTF importers share a preamble and a mesh loop

[`MeshCook.cpp`](../../engine/src/assets/cook/MeshCook.cpp) `ImportGltfScene`
(line 761, 237 lines) and `ImportGltfMeshes` (line 662, 83 lines) open with an
identical 30-line cgltf preamble — parse, error message, `CgltfDataPtr`,
`cgltf_load_buffers` with the same external-URI rejection string, zero-mesh
check (765-790 against 666-693) — and then run the same per-mesh name
resolution and primitive accumulation. `ImportGltfScene` is a strict superset
that additionally handles skeletons, skin remapping, and animation clips.

Both are public API in
[`MeshCook.h`](../../engine/include/assets/cook/MeshCook.h) with real separate
callers, so neither can be deleted. Extract two anonymous-namespace helpers in
the same file:

- `ParseGltf(bytes, CgltfDataPtr& out, std::string* error)`, which removes the
  preamble duplication outright;
- `AccumulatePrimitives(mesh, nameForErrors, ImportedGltfMesh&, std::vector<MeshSkinInfluence>* influences, std::string* error)`,
  the shared geometry loop, taking its skinning output optionally exactly as
  `ReadPrimitive` already does.

This is well protected. `test/core/MeshCookTests.cpp` has 14 tests, including
`ExternalBufferUriIsRejected`, `MalformedBytesAreRejected`, and
`ImportIsDeterministic`, all of which exercise the shared preamble;
`test/core/SkeletalCookTests.cpp` has 4 tests on `ImportGltfScene`;
`test/level_cook/BrushBakeTests.cpp` uses the mesh path. Both sides of the
extraction are covered.

### 2.3 `MeshForwardPass` writes its pipeline description twice

[`MeshForwardPass.cpp`](../../engine/src/render/MeshForwardPass.cpp)
`EnsurePipelines` (line 136) and `EnsureDebugPipelines` (line 206) contain about
35 byte-identical lines of vertex bindings and ten vertex attributes. Adding a
vertex attribute today means editing two identical lists.

Extract a file-local `MakeMeshPipelineBase()` returning `GraphicsPipelineDesc`;
each caller then overrides roughly six fields. `GraphicsPipelineDesc` is a value
type with a defaulted `operator==`, so equivalence stays mechanically checkable
by inspection even without a test. There is no test coverage; verify by running.

### 2.4 The two lightmap bake channels share nine parameters

[`DocumentLightmapBake.cpp`](../../editor/kyusu/src/document/DocumentLightmapBake.cpp)
`BakeFreshDirect` (line 120) and `BakeFreshAo` (line 165) take 12 parameters
each, of which nine are identical and in identical order. Only the out-artifact
differs. Both are file-local statics in an anonymous namespace, so a
`BakeChannelContext` struct costs no API churn and no header edit, and the two
call sites collapse to two lines each. Lowest-risk item in the set.

## 3. Parameter lists

The codebase carries 80-plus `*Desc`, `*Params`, `*Services`, and `*Context` POD
structs. Descriptor structs are the established idiom here, so these introduce
no new mechanism.

### 3.1 `Renderer::Renderer`

[`Renderer.cpp`](../../engine/src/graphics/vulkan/Renderer.cpp) line 38 takes 15
parameters, and its body is an `IsValid()` conjunction followed by 14
consecutive `Services.X = &x;` assignments into a member of type
`RendererServices` — a struct that already exists in
[`Renderer.h`](../../engine/include/graphics/vulkan/Renderer.h) with exactly
those fields.

Taking `const RendererServices&` deletes the assignment block entirely. The
`Log(logging.GetLogger<Renderer>())` member initializer becomes
`services.Logging->GetLogger<Renderer>()`.

On the ABI question: `engine/include/` is installed wholesale, so `Renderer.h`
is a public SDK header. But
[`check_module_abi.sh`](../../scripts/check_module_abi.sh) explicitly bars
`graphics/` from the hashed game-module ABI surface, whose dirs are `app`,
`input`, `world/serialization`, `core/metadata`, `core/console`, and `ecs`. This
is a host-level SDK change, not a module ABI break. The only in-tree
construction site is a single member initializer in
[`GraphicsServices.cpp`](../../engine/src/graphics/vulkan/GraphicsServices.cpp)
at line 68. Run the ABI and isolation checks regardless.

### 3.2 `RenderExtractionSystem::Extract`

[`RenderExtractionSystem.cpp`](../../engine/src/render/RenderExtractionSystem.cpp)
line 82 takes 9 parameters, four of which are read-only caches
(`StaticMeshCache`, `MaterialCache`, `MaterialSetCache`, `TextureCache*`). A
`RenderExtractCaches` struct in the owning header reduces the call to
`Extract(world, partitions, caches, camera, queue, alpha)`.

Signature only. The body stays as it is; it already delegates to three extracted
free functions covered by `test/runtime/ZoneLightmapResolutionTests.cpp`.

### 3.3 Deferred and declined

`EditorRenderFeature`'s 14-parameter constructor fans its arguments out into
about 12 different sub-renderer member initializers, so a services struct is
passed through rather than stored and every initializer still needs
`services.X` qualification. Real but modest benefit against meaningful churn;
deferred.

`TimingSampler::PushRenderFrame` is declined. All nine parameters are already
distinct aggregates, and `BuildBaseSample` already factors what it shares with
`PushLifecycleFrame`. Wrapping aggregates in another aggregate is bureaucracy.

## 4. Analyzer retuning

This is the largest part of the count and the smallest part of the diff.

No suppression goes in source comments. A wall of inline annotations across the
render layer is exactly the narration this repository's comment rules prohibit.
Dispositions are recorded in a checked-in `.codacy.yaml` at the repository root
— none exists today, so there is currently nowhere to record any of this — plus
per-pattern threshold changes on the repository's coding standard.

### 4.1 Lizard thresholds

`Lizard_nloc-medium` is 387 of the 626 findings repository-wide and 79 of the
rendering 114. A 50-line method limit is not a defensible standard for Vulkan
object creation, glTF attribute unpacking, or descriptor-set construction. This
is the pattern misfiring at scale.

Raise the method threshold to 150 non-comment lines, which still catches
`ImportGltfScene` at 237 and `ScheduleViews` at 210 while clearing the
verbose-but-atomic Vulkan blocks, and the file threshold to roughly 900. Then
re-review the survivors, which will be a short list.

### 4.2 Flawfinder `memcpy`

Disable the pattern for this repository. It fires on every `memcpy` regardless
of context and cannot distinguish a `resize()` on the preceding line from an
unchecked copy. Thirteen of the sixteen rendering hits are provably sized copies
into fixed arrays with clamped counts, or into scratch-ring grants sized by the
same `sizeof` expression. It has a 100 percent false-positive rate here and a
100-finding footprint repository-wide. Section 5 covers this class properly.

An optional cosmetic follow-up, not required: `MeshSerializer.cpp` line 49 and
`TextureSerializer.cpp` line 64 are `ostringstream::str()` to `vector<byte>`
handoffs that read more clearly as `out.assign(...)`.

### 4.3 The three Cppcheck dispositions

**`ShadowResidency.cpp` lines 578 and 614, "condition `budget > 0` is always
true": false positive, and acting on it introduces a bug.** `budget` is
decremented inside two lambdas that capture it by reference (lines 439 and 465),
and Cppcheck's value flow does not model mutation through a deferred
by-reference capture, so it concludes the guard is dead. It is not. The inner
loops run up to `kPointShadowFaceCount` (6) times, and at lines 611-612
`PendingFaces` is refilled to all six faces immediately before the loop. With
`MaxViewsPerFrame` of 1, dropping the guard takes `budget` from 1 to 0 to
`UINT32_MAX`, silently uncapping per-frame shadow views for the rest of the
frame. Note also that `MaxViewsPerFrame == 0` means unlimited, not disabled
(line 410, `LightingPanel.cpp` line 187, `ShadowResidencyTests.cpp` line 58).
The identical construct at line 636 was not flagged, which confirms a checker
bailout rather than a property of the code. The behavior is covered by 28 tests
in `test/runtime/ShadowResidencyTests.cpp`.

**`TextureCook.cpp` line 261, "assigned value is always true": benign.** It is
the thread-safe run-once static idiom; the bool is a sentinel, discarded by the
`(void)initialized;` on the next line. `EnsureEncodersInitialized` is called from
job threads, and `rgbcx::init()` and `bc7enc_compress_block_init()` build
non-reentrant global tables. Keep it.

**`ProbeBake.cpp` lines 182 and 183, "scope can be reduced": false positive, and
acting on it is a performance regression.** `nextFilled` and `nextSh` are
deliberately hoisted ping-pong buffers, documented in the comment directly above
them, copy-assigned each round so they reuse capacity, and `std::swap`ped at the
end of the round. Moving them inside the loop forces a fresh allocation and free
of two grid-sized buffers per dilation round.

### 4.4 The one correct finding

`check_render_portability.sh` line 48, ShellCheck SC2001: use
`${variable//search/replace}` instead of `sed`. One line, in the render layer's
own CI guard.

## 5. Wiring up clang-tidy

[`.clang-tidy`](../../.clang-tidy) already exists, enabling `bugprone-*` and
`performance-*` with `HeaderFilterRegex` scoped to
`engine/(src|include)/(render|graphics|profiling)/`. It was added by commit
`e1c5b9ea` and runs from neither CMake nor CI, and Codacy's hosted Clang-Tidy
produces zero findings.

Add `scripts/run_render_tidy.sh` alongside the existing `check_*.sh` guards,
driving `run-clang-tidy` over the render, graphics, and profiling translation
units from a `compile_commands.json`.

Two constraints shape this. First, it cannot run where the other guards run: the
existing `check_*.sh` are read-only source greps run deliberately before the
build, and clang-tidy needs a compile database, so it must run after configure.
Add `CMAKE_EXPORT_COMPILE_COMMANDS` to the dev preset if it is not already set.
Second, it starts advisory rather than gating. This codebase has never run
clang-tidy, so turning it on surfaces a fresh unbounded finding set; land the
script, run it, read the output, and decide gating separately. Do not add a
failing CI leg in the same change.

Prefer the script over `CMAKE_CXX_CLANG_TIDY`, which would slow every developer
build, and over Codacy's hosted Clang-Tidy, which cannot see this repository's
build flags or Vulkan include paths and would report unreliably.

## 6. Declined, with reasons

Recorded so they are not re-opened.

| Site | Reason |
| --- | --- |
| `VulkanPipelineCache::CreateGraphicsPipeline` (148 lines) | `VkGraphicsPipelineCreateInfo` holds nine raw pointers into locals that must outlive `vkCreateGraphicsPipelines`. Splitting yields dangling pointers. Permanent. |
| `LightBindings` `CreateDummies`, `CreateSetObjects`, `CreateCubePool` | `bindings[]`, `bindingFlags[]`, and `flagsInfo` are pointed to by `layoutInfo` and must share a scope. The file already extracts `transition`, `destroyPool`, and `WriteBinding`. |
| `VulkanFrameService::BeginFrame` and `EndFrame` | Roughly 60 percent error handling; every phase returns the same status to the same caller and mutates the same frame slot. Extraction is net negative. An optional three-line `StatusFor(VkResult)` would tidy the five repeated device-lost mappings. |
| `VulkanImageService::Create` and `RecordMipChain` | `RecordMipChain`'s correctness depends on the reader seeing the whole per-mip layout ping-pong at once. |
| `MeshCook::ReadPrimitive`, `GltfMeshImporter::Import` | Per-attribute glTF unpacking; splitting yields six functions writing into the same vector. |
| `MeshForwardPass::UploadFrameUniforms`, `Draw`, `DrawRuns` | Flat struct marshalling and redundant-bind elision. `Draw` is already an orchestrator over the existing private helpers. |
| `ShadowResidency.cpp` file size, 822 lines | One cohesive state machine whose contract is documented in [`ShadowResidency.h`](../../engine/include/render/ShadowResidency.h); `Update()` reads as an 11-line pipeline. Splitting produces two files sharing the same private class state. |
| `MeshLoader.cpp` line 44 `memcpy` | A correct `streambuf::xsgetn` override with the `min` bound as its check. |

## 7. Order of work

Independently reviewable commits on `render-codacy-findings`. Never push to
`main`.

1. Analyzer retuning: `.codacy.yaml`, the coding-standard thresholds, and the
   SC2001 one-liner. No engine code. Re-run analysis and record the new
   baseline.
2. Texture path defects, items 1.1 and 1.2, with regression tests. Small,
   test-protected, highest actual value.
3. glTF import de-duplication, item 2.2. Largest diff, fully test-protected.
4. Lightmap bake context struct, item 2.4. File-local and trivial.
5. `RendererServices` constructor and `RenderExtractCaches`, items 3.1 and 3.2.
   Signature only; run the ABI and isolation checks.
6. Vulkan upload de-duplication and region bounds, items 2.1 and 1.3. Blind, and
   verified by running the app. Lands last and alone so a visual regression has
   an unambiguous cause.
7. The clang-tidy script, section 5, advisory only.

`MakeMeshPipelineBase` (2.3) can ride with step 6 or stand alone; it is also
blind.

## 8. Verification

Focused, per step:

```sh
ctest --test-dir build -R 'TextureAsset|TextureCook' --output-on-failure
ctest --test-dir build -R 'MeshCook|SkeletalCook|BrushBake' --output-on-failure
ctest --test-dir build -R 'ShadowResidency|ZoneLightmapResolution' --output-on-failure
./scripts/check_module_abi.sh .
./scripts/check_render_portability.sh
```

Full gate before each push:

```sh
cmake --preset dev
cmake --build --preset dev --parallel
ctest --preset dev
git diff --check
```

`VulkanImageService`, `VulkanPipelineCache`, `VulkanFrameService`, `Renderer`,
`MeshForwardPass`, and `LightBindings` have no references anywhere under
`test/`; there is no Vulkan device harness in this repository. Steps 5 and 6 are
confirmed by running the app:

- SceneViewer on a textured, mip-mapped, shadow-casting scene, since the mip
  chain and both upload paths are only exercised by real texture loads;
- the Kyusu viewport, which confirms `Renderer` construction and the editor
  render feature still come up.

Compare against a pre-change capture, running foreground with
`SENCHA_PRESENT_MODE=IMMEDIATE`.

The regression tests for items 1.1 and 1.2 must be confirmed to fail before the
fix, for the intended reason.
