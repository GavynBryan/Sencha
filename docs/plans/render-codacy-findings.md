# Rendering and Graphics Codacy Findings

Status: triaged and implementation-reviewed, not started. Every finding below
was read against source; the verdicts are recorded so they do not get
re-litigated.

Sencha was connected to Codacy on 2026-08-15. The first analysis ran against
`e9f9a32d` using Codacy's unmodified default coding standard and produced 626
findings. Nobody selected those patterns for this codebase, so the set is a raw
tool baseline rather than a reviewed defect list.

The triage below was performed against `e9f9a32d` and re-checked at `d3e53e1a`.
The commits between the two touch `app/`, `ecs/`, `input/`, `net/`,
`participant/`, `world/`, `template/`, and `test/`, but none of the rendering,
graphics, or graphics-asset paths reviewed here. No finding, verdict, or line
reference is affected by them.

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
content is small: two defects in the cooked-texture path, one upload-service
validation family affecting both entry points, and four cases of duplication
that the line-count patterns surfaced as a side effect. Two of the "always
true" findings are wrong in a way that matters — acting on them introduces an
unsigned underflow — and one "reduce scope" finding would cost a
multi-megabyte allocation per bake round if applied.

The goal is therefore not a zero issue count. It is to fix what is broken,
remove the duplication the tool correctly pointed at, and retune the analyzer so
its future output is signal. Separately, the repository already carries a
render-scoped [`.clang-tidy`](../../.clang-tidy) that runs from neither CMake nor
CI. Section 5 expands it to the graphics asset pipeline and runs it with the
repository's real compile database.

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

The overlap check does not prove that `PixelDataOffset` itself lies inside the
stream, so the subtraction needs its own offset guard:

```cpp
if (header.PixelDataOffset > bytes.size()
    || header.PixelDataSize > bytes.size() - header.PixelDataOffset)
```

Regression coverage goes in `test/core/TextureAssetTests.cpp`:

- a stream with a wrapping `PixelDataSize` is rejected with the existing
  `"stex: pixel data out of range"` diagnostic rather than throwing;
- a `PixelDataOffset` beyond the end of the stream is still rejected, protecting
  the subtraction form from reintroducing an offset underflow.

The wrapping-size test must be observed failing against the old check for the
intended reason. The offset-only case preserves behavior the old addition check
already handled.

### 1.2 `Image` carries an unenforced size invariant

[`TextureCook.cpp`](../../engine/src/assets/cook/TextureCook.cpp) line 219 copies
`texture.Mips[0].ByteSize` — which is `Width * Height * 4`, derived purely from
the declared dimensions — out of `image.Pixels`. The only guard is
`image.IsValid()`, and [`Image`](../../engine/include/render/Image.h) defines
that as `!Pixels.empty() && Width > 0 && Height > 0`. It never checks that
`Pixels.size()` agrees with the dimensions.

The runtime producers today are `LoadImageFromFile` and `LoadImageFromMemory`
([`ImageLoader.cpp`](../../engine/src/render/ImageLoader.cpp)), which assign
exactly `w * h * 4` from stb_image. Tests already hand-construct valid images;
the first inconsistent fixture, procedural texture, or future decoder would
turn the latent over-read into a real one.

The invariant belongs to `Image`, not to the copy site, so `Image::IsValid()`
must require an exact tightly packed RGBA buffer. Avoid forming
`Width * Height * 4` in a fixed-width type: after rejecting zero dimensions,
compare `Pixels.size() / BytesPerPixel()` with the `uint64_t` product
`Width * Height` and require a zero remainder. The product of two `uint32_t`
dimensions fits in `uint64_t`; multiplying that result by four does not always
fit.

`Image::ByteSize()` should return the actual owned storage size,
`Pixels.size()`, as `std::size_t`, rather than recomputing a second potentially
overflowing value from the dimensions. Its only runtime consumer already casts
to `VkDeviceSize`. This is an intentional host-SDK source change in an installed
inline header, but it does not touch the game-module ABI.

The owning `Image` tests in `test/engine_features/AssetTests.cpp` cover exact,
undersized, oversized, and extreme-dimension buffers, plus `ByteSize()`
reporting the actual owned bytes. A compile-time assertion locks its
`std::size_t` result type without requiring a multi-gigabyte allocation. At
least the undersized and oversized validity tests must fail before the fix
without invoking the unsafe cook copy.

### 1.3 The Vulkan upload boundary trusts caller-sized staging buffers

[`VulkanImageService.cpp`](../../engine/src/graphics/vulkan/VulkanImageService.cpp)
line 448 validates only `region.Offset >= size` for each region, never that the
region *ends* inside the staging buffer. The `vkCmdCopyBufferToImage` footprint
comes from the image format and `region.Width`/`region.Height`, so a region near
the end of the blob makes the GPU read past the staging allocation.

This is safe today only because `TextureCache::UploadGpuImage` runs
`ValidateTextureData` first, which proves the mip table exactly tiles the blob.
`UploadMips` is a public service API that a future caller can reach without the
validator, so the complete copy contract belongs at the service boundary.

`VulkanImageService::Upload` has the same gap: it allocates the caller's `size`
but records a copy of the entry's full width, height, depth, and format. All
three current callers provide the correct size, but a short public-API call can
still make the GPU read past its staging allocation.

Add one GPU-independent validation mechanism at the Vulkan image boundary. It
computes tight copy footprints for the formats the service currently uploads
and validates before staging allocation or command recording:

- `Upload`: the available size covers the full base-mip extent, including depth;
- `UploadMips`: every target mip appears exactly once, its dimensions match the
  image's mip extent, its offset satisfies the format's block alignment, and
  its computed footprint fits in `size - Offset` without addition overflow;
- both paths: the format is one whose block dimensions and byte size the
  validator understands, and the entry uses the color aspect the current copy,
  blit, and barrier path supports.

Do not add a caller-authored `ByteSize` to `MipUploadRegion`: checking a claimed
size would not prove how many bytes Vulkan reads. If depth or stencil upload is
needed later, extend the copy and every barrier together; do not switch only the
whole-chain barriers to `entry->AspectMask`.

The validator is a narrow test boundary justified by the lack of a device
harness. Extract the current permissive checks into it first and wire both
service methods through it, then add malformed base-size, region-end,
wrong-extent, duplicate/missing-mip, alignment, format, and aspect cases. The
new rejection cases must be observed failing before the checks are tightened.

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

Keep the upload path explicitly color-only as item 1.3 specifies; the copy,
optional blit, and barriers then agree on one supported aspect.

Use an anonymous-namespace RAII `StagingBuffer` plus a
`TransitionWholeChain(cmd, entry, from, to)` helper, both file-local in
`VulkanImageService.cpp`. `StagingBuffer` owns creation, mapped copy, flush, and
destruction; command-buffer acquisition remains visibly owned by the service
method. These are private details of one service with no second consumer, so a
separate translation unit would be a seam with no boundary behind it. The
result halves both functions and makes the actual difference visible — one
`VkBufferImageCopy` against N.

The pure validation mechanism from item 1.3 protects the boundary behavior.
There is still no Vulkan device harness for the staging and command-recording
shape, so exercise both valid upload paths in the application after the
refactor.

### 2.2 The glTF importers share a preamble and a mesh loop

[`MeshCook.cpp`](../../engine/src/assets/cook/MeshCook.cpp) `ImportGltfScene`
(line 761, 237 lines) and `ImportGltfMeshes` (line 662, 83 lines) open with an
identical 30-line cgltf preamble — parse, error message, `CgltfDataPtr`,
`cgltf_load_buffers` with the same external-URI rejection string, zero-mesh
check (765-790 against 666-693) — and then run the same per-mesh name
resolution and primitive accumulation. `ImportGltfScene` additionally handles
skeletons, skin remapping, and animation clips.

Both are public API in
[`MeshCook.h`](../../engine/include/assets/cook/MeshCook.h) with real separate
callers, so neither can be deleted. Extract two anonymous-namespace helpers in
the same file:

- `ParseGltf(bytes, CgltfDataPtr& out, std::string* error)`, which removes the
  preamble duplication outright;
- `ReadMeshGeometry(mesh, nameForErrors, ImportedGltfMesh&, std::vector<MeshSkinInfluence>* influences, std::string* error)`,
  which owns the zero-primitive check, primitive accumulation, section assembly,
  bounds recomputation, and `ValidateMeshGeometry` error joining. Its skinning
  output is optional exactly as `ReadPrimitive` already allows.

This is well protected. `test/core/MeshCookTests.cpp` has 14 tests, including
`ExternalBufferUriIsRejected`, `MalformedBytesAreRejected`, and
`ImportIsDeterministic`, all of which exercise the shared preamble;
`test/core/SkeletalCookTests.cpp` has 5 tests on `ImportGltfScene`; and
`test/level_cook/BrushBakeTests.cpp` includes the `GltfMeshExport` round trip.
Both sides of the extraction are covered.

### 2.3 `MeshForwardPass` writes its pipeline description twice

[`MeshForwardPass.cpp`](../../engine/src/render/MeshForwardPass.cpp)
`EnsurePipelines` (line 136) and `EnsureDebugPipelines` (line 206) contain about
35 byte-identical lines of vertex bindings and ten vertex attributes. Adding a
vertex attribute today means editing two identical lists.

Extract a file-local `MakeMeshPipelineBase()` returning `GraphicsPipelineDesc`;
each caller then overrides its shaders, formats, depth state, and blend state.
There is no direct test coverage; keep this as a stand-alone change and verify
the normal and debug/overdraw pipelines by running the application.

### 2.4 The lightmap bake channels re-expand an existing context

[`DocumentLightmapBake.cpp`](../../editor/kyusu/src/document/DocumentLightmapBake.cpp)
`BakeFreshDirect` (line 120) and `BakeFreshAo` (line 165) take 12 parameters
each, of which 11 are identical and in identical order. Only the out-artifact
differs. Seven of those common arguments are already owned by
[`DocumentCookContext`](../../editor/kyusu/src/document/DocumentCookContext.h),
whose stated purpose is to keep cook stages from re-listing this cluster.

Pass the existing `DocumentCookContext` plus the four channel inputs and the
output artifact to each file-local function. Do not introduce a second
`BakeChannelContext`. The focused `BakedLightingCookTest` suite covers both
fresh channels and reuse behavior.

## 3. Parameter lists

The codebase carries 80-plus `*Desc`, `*Params`, `*Services`, and `*Context` POD
structs. That makes a descriptor an established option, not an automatic answer
to every long signature.

### 3.1 `Renderer::Renderer`

[`Renderer.cpp`](../../engine/src/graphics/vulkan/Renderer.cpp) line 38 takes 15
parameters, but the shape is the explicit Vulkan composition root. Fourteen
arguments become feature-visible pointers in `RendererServices`; the remaining
`VulkanFrameService&` initializes the renderer's private `Frames` reference and
is deliberately absent from the feature bundle.

Replacing the constructor with only `const RendererServices&` therefore cannot
initialize the current object. Adding `Frames` to that bundle would expose an
unneeded backend service to every render feature and would weaken non-null
constructor references into nullable pointers. There is one in-tree
construction site and no duplicated policy or resolution logic. Decline this
finding: the long signature accurately spells the ownership graph.

### 3.2 `RenderExtractionSystem::Extract`

[`RenderExtractionSystem.cpp`](../../engine/src/render/RenderExtractionSystem.cpp)
line 82 takes 9 parameters, four of which are read-only caches
(`StaticMeshCache`, `MaterialCache`, `MaterialSetCache`, `TextureCache*`). A
`RenderExtractCaches` struct in the owning header reduces the call to
`Extract(world, partitions, caches, camera, queue, alpha)`.

The bundle contains references for the three required caches and the existing
optional `TextureCache*`. Add the bundled overload as the preferred entry point,
but keep the installed nine-parameter overload as a forwarding compatibility
shim. The extraction body and hot-path work stay unchanged; constructing the
bundle allocates nothing. `ZoneLightmapResolutionTests` protects the pure
partition-to-lightmap pieces, while the full build protects both public
signatures and the one in-tree caller.

This adds a host-SDK API without removing the old one and does not change the
game-module ABI. Run the module isolation check anyway because the owning header
is installed.

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
This document is the checked-in technical record of the dispositions. Codacy's
`.codacy.yaml` supports path exclusions and limited engine configuration, not
per-issue dispositions or these pattern parameters, so do not add an otherwise
empty file for that purpose. Adding one also supersedes ignored-file settings
from the Codacy UI; if a future path exclusion requires the file, inventory and
preserve every existing ignored path first.

Apply pattern and threshold changes in this repository's Code Patterns settings,
not to an organization-wide standard shared by unrelated repositories. Record
the exact settings, analysis date, tool versions, and resulting count in this
section when the retuning is performed. Mark individual retained findings in
Codacy with the appropriate false-positive or accepted-use reason and a concise
comment pointing to the invariant recorded here.

### 4.0 What was applied, 2026-08-16

Against the analysis of record for `d3e53e1a` (624 findings; the 626 first
reported were against `e9f9a32d`, and two net/participant commits cleared two).

Applied at the repository level, which is where Codacy accepts parameter
changes even while a tool follows a coding standard:

| Setting | From | To |
| --- | --- | --- |
| `Lizard_nloc-medium` threshold | 50 | 150 |
| `Lizard_file-nloc-medium` threshold | 500 | 900 |
| `Lizard_parameter-count-medium` threshold | 8 | 8 (unchanged) |

The five Cppcheck findings in section 4.3 are marked ignored as false
positives (`PATCH .../issues/{issueId}` with `{"ignored": true}`): both
`ShadowResidency` budget guards, the `TextureCook` run-once sentinel, and both
`ProbeBake` ping-pong buffers. The pattern stays enabled.

Two items could not be completed and are not done:

- **`flawfinder_memcpy` is still enabled.** The full review section 4.2 requires
  is complete — all 100 findings were read, and all 100 are benign, so the
  pattern contributes no signal in this repository and meets the plan's bar for
  disabling. Codacy refuses: *"Cannot disable a pattern that is enabled by a
  Coding Standard."* The standard governing it (`Default coding standard`, id
  165914, applied to Sencha alone but flagged as the organization default)
  exposes only GET and DELETE over the API, so disabling the pattern means
  cloning the standard, editing the clone, promoting it, and re-pointing the
  repository — or detaching the repository from standards entirely. Both are
  structural changes to the Codacy account rather than the threshold retuning
  this section authorizes, so they are left for an explicit decision. Doing it
  through the Codacy UI's Code Patterns page is the smaller path.
- **The post-retune baseline is not recorded.** Codacy applies pattern changes
  on the next analysis rather than recomputing stored results, and the API
  exposes no way to trigger one. The counts above are therefore still the
  pre-retune numbers. The new baseline lands when this branch is pushed and
  analyzed, and belongs in this table then.

The full `memcpy` review is worth keeping rather than repeating. Every finding
falls into one of: a `std::min` clamp into a fixed buffer (the editor and
`InlineString` string paths), a `resize` immediately above the copy (the
serializers, the `ostringstream` handoffs, the test fixtures), a `sizeof(T)`
copy into or out of a `T` guarded by an equality check on the byte count (the
ECS schema and component paths), an ECS column copy whose stride is asserted
equal on both sides, a bounds-checked reader or writer (`NetWriter::Reserve`,
the animation and skeleton readers), or a fixed-size protocol field
(`UdpTransport`'s 4- and 16-byte address copies). The audio loader deserves a
specific mention: `AudioClipSerializer` already compares sample counts rather
than byte products and bounds its offset before subtracting, which is the same
shape section 1.1 had to introduce in `TextureLoader`.

### 4.1 Lizard thresholds

`Lizard_nloc-medium` is 387 of the 626 findings repository-wide and 79 of the
rendering 114. A 50-line method limit is not a defensible standard for Vulkan
object creation, glTF attribute unpacking, or descriptor-set construction. This
is the pattern misfiring at scale.

After the defect and de-duplication work lands, raise the method threshold to
150 non-comment lines and the file threshold to 900. This still catches
`ImportGltfScene` at 237 and `ScheduleViews` at 210 while clearing the
verbose-but-atomic Vulkan blocks. Re-run analysis and review every survivor
before recording the new baseline.

### 4.2 Flawfinder `memcpy`

Keep the pattern enabled until all 100 repository-wide findings have been
reviewed. The rendering triage proves only that its 16 findings are benign: the
tool fires on every `memcpy` and cannot distinguish a `resize()` on the
preceding line from an unchecked copy. That is evidence of poor precision in
this domain, not evidence that the other 84 calls are safe.

Export the complete finding list, classify every call, fix any real size or
lifetime defects first, and record the result here. Disable the pattern at the
repository level only if the complete review establishes that it contributes no
signal. Otherwise keep it enabled and disposition the reviewed false positives
individually. Section 5 deliberately includes `assets/`, but clang-tidy is a
complement, not proof that every `memcpy` has a valid data-dependent bound.

### 4.3 The three Cppcheck dispositions

Keep the pattern enabled and mark these individual issues as false positives in
Codacy; none warrants a source suppression.

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
`engine/(src|include)/(render|graphics|profiling)/`. It has been run during a
past render audit but runs from neither CMake nor CI today. The current scope
also excludes `assets/`, where this triage found both CPU-side texture defects.

Add `scripts/run_render_tidy.sh` alongside the existing `check_*.sh` guards,
driving `run-clang-tidy` over the `assets`, `render`, `graphics`, and `profiling`
translation units from a `compile_commands.json`. Expand `HeaderFilterRegex` to
the same four engine subtrees. The script accepts a build directory, verifies
that its compile database and the required tools exist, prints the clang-tidy
version, and returns the analyzer's status.

`CMAKE_EXPORT_COMPILE_COMMANDS` is already enabled in the base preset and at the
root CMake level, so no configure change is needed. Run after the build, not
merely after configure: generated headers and compiled shader artifacts must
exist before clang-tidy parses every translation unit.

Run the script locally first, fix or record every actionable finding, and save
the initial versioned baseline. Then install the distro-pinned `clang-tidy` in
the Linux CI job and add a post-build advisory step with `continue-on-error` and
a bounded step timeout. Capture its output in the job log. It starts advisory
because this repository has no continuously maintained baseline; converting it
to a required gate is a separate decision after the output is stable.

Prefer the script over `CMAKE_CXX_CLANG_TIDY`, which would slow every developer
build, and over Codacy's hosted Clang-Tidy, which cannot see this repository's
build flags or Vulkan include paths and would report unreliably.

### 5.1 Initial baseline

Recorded 2026-08-16 from `scripts/run_render_tidy.sh build`, LLVM 21.1.8, over
106 translation units.

The first run reported 158 findings, 102 of them `performance-enum-size`. That
check is wrong for this layer rather than informative: GPU-facing, cooked, and
wire-facing enums carry the width their format specifies, and shrinking one to
fit its current value set would change a layout the other side reads. It is
disabled in `.clang-tidy` with that reason, which leaves the baseline at **56**:

| Check | Count |
| --- | --- |
| `bugprone-unchecked-optional-access` | 18 |
| `bugprone-implicit-widening-of-multiplication-result` | 8 |
| `performance-inefficient-string-concatenation` | 6 |
| `bugprone-incorrect-roundings` | 2 |
| `bugprone-suspicious-memory-comparison` | 1 |
| `bugprone-multi-level-implicit-pointer-conversion` | 1 |
| `bugprone-inc-dec-in-conditions` | 1 |
| `bugprone-suspicious-include` | 1 |
| `performance-unnecessary-value-param` | 1 |
| `performance-unnecessary-copy-initialization` | 1 |

Note that a `#` cannot appear inside `.clang-tidy`'s `Checks:` block: it is a
YAML folded scalar, so the comment becomes part of the check string and
silently disables nothing.

None of these are triaged yet, and triaging them is not part of this document's
scope — the run is advisory precisely so the list can be worked down
deliberately. Two are worth naming as the most likely to be real:

- `MeshCook.cpp:748` compares `StaticMeshVertex` with `memcmp` though the type
  has no unique object representation. This is the vertex weld path, where
  padding bytes deciding equality would affect which vertices merge. It sits on
  a determinism-sensitive path, so it wants its own change with its own test
  rather than a drive-by fix.
- `bugprone-implicit-widening-of-multiplication-result` is the same defect class
  as sections 1.1 and 1.2. The one instance checked so far,
  `TextureCook.cpp:249`, is a false positive: the indices reach at most 60 in a
  64-byte block and the source index already widens. The other seven are unread.

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

Use independently reviewable work units on `render-codacy-findings`; keep each
unrelated code change in its own commit and never push to `main`.

1. Texture path defects, items 1.1 and 1.2, with owning regression tests. Small,
   test-protected, highest immediate value.
2. Vulkan upload validation, item 1.3: extract and wire the behavior-preserving
   pure validator, observe the malformed cases fail, then tighten both service
   entry points. Keep this separate from staging refactoring.
3. glTF import de-duplication, item 2.2. Largest diff, fully test-protected.
4. Reuse `DocumentCookContext` in the lightmap channels, item 2.4. File-local and
   covered by end-to-end cook tests.
5. Add `RenderExtractCaches` while retaining the forwarding compatibility
   overload, item 3.2. Run the host build and module isolation checks.
6. Vulkan staging and barrier de-duplication, item 2.1. Validation behavior is
   already protected by step 2; exercise both valid upload paths in the app.
7. `MakeMeshPipelineBase`, item 2.3, as its own change so any visual regression
   has an unambiguous cause.
8. Expand and wire the advisory clang-tidy run from section 5, run it locally,
   and record its initial versioned baseline before adding the advisory CI step.
9. Analyzer retuning from section 4 and the SC2001 correction. Review all 100
   Flawfinder findings before deciding its repository-wide status, apply Lizard
   settings repository-locally, re-run Codacy, and record the new baseline here.

## 8. Verification

Focused, per step:

```sh
ctest --test-dir build -R '^(Image|TextureData|StexRoundTrip|TextureCook|TextureAssetLoaderStex)\.' --output-on-failure
ctest --test-dir build -R '^VulkanImageUploadValidation\.' --output-on-failure
ctest --test-dir build -R '^(MeshCook|SkeletalCook|BrushBake|GltfMeshExport)\.' --output-on-failure
ctest --test-dir build -R '^BakedLightingCookTest\.' --output-on-failure
ctest --test-dir build -R '^(ShadowResidency|ZoneLightmapResolution)\.' --output-on-failure
./scripts/check_module_abi.sh .
./scripts/check_render_portability.sh
```

After item 1, exercise the malformed-image coverage under AddressSanitizer:

```sh
cmake --preset asan
cmake --build --preset asan --parallel
ctest --test-dir build-asan -R '^(Image|TextureData|StexRoundTrip|TextureCook|TextureAssetLoaderStex)\.' --output-on-failure
```

Full gate before each push:

```sh
cmake --preset dev
cmake --build --preset dev --parallel
ctest --preset dev
git diff --check
```

`VulkanImageService`, `VulkanPipelineCache`, `VulkanFrameService`, `Renderer`,
`MeshForwardPass`, and `LightBindings` have no device-backed tests. The pure
validator covers rejection without a GPU; the staging and pipeline changes are
confirmed by running:

- SceneViewer on a cooked, mip-mapped, shadow-casting scene with a baked probe
  volume, covering `UploadMips`, the 3D `Upload` path, and normal forward
  pipelines;
- the Kyusu viewport with the editor skin loaded, covering the 2D `Upload` path;
- the renderer's debug and overdraw views, covering both pipeline-base consumers.

Compare against a pre-change capture, running foreground with
`SENCHA_PRESENT_MODE=IMMEDIATE`.

After the dev build, run the advisory analyzer and preserve its output with the
tool version:

```sh
./scripts/run_render_tidy.sh build
```

The regression tests for items 1.1 and 1.2, and the newly rejected validation
cases for item 1.3, must be confirmed to fail before their respective checks are
tightened, for the intended reasons described above.
