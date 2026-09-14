# Authored UI

Sencha's retained, authored presentation layer. RML describes structure, RCSS
describes presentation, and native code owns state and behaviour. Games and
Sencha applications host the same substrate: a HUD, a pause menu, Kyusu's
inspector and its asset browser are the same mechanism with different content.

RmlUi implements it. RmlUi is not the architecture, and nothing outside
`engine/src/ui/rml/` may name it.

Status: Stage 1 (boundary and phases) has landed. The runtime itself, the `.sui`
content path, and the render feature are Stages 2 and 3. Sections describing
those are the contract they will be built against, not a description of code that
exists. Where that distinction matters it is called out.

---

## 1. The directional boundary

```
Application / Game state
        |  copied presentation values
        v
  Presentation model
        v
   UI document
        v
   UiDrawFrame
        v
    Renderer

Semantic actions
        ^
   UI document
```

State travels down as **copies**. Intent travels back up as **semantic actions**
carrying owned payloads. No pointer crosses in either direction -- not an element
pointer, not a DOM node, not an entity, not an editor object, not a callback a
module owns.

A UI document's own mutable state is presentation only: focus, text being typed,
a transient field value, selection inside a control, scroll offset, animation,
modal state. Everything authoritative stays with the game or the application, and
a UI control never commits it directly. A number typed into an inspector field
becomes a semantic action; the editor controller validates it, builds the
undoable command, and republishes the resulting model.

## 2. Layout of the code

```
engine/include/ui/        PUBLIC. Sencha concepts. No RmlUi type, ever.
engine/src/ui/            PRIVATE. The document-engine integration, and the
                          only place <RmlUi/...> or Rml:: may appear.
engine/src/ui/rml/        The interface implementations, grouped because they
                          are a family -- not a second firewall.
engine/include/render/ui/ Render-domain UI data. Plain CPU values, no backend.
engine/include/render/feature/UiRenderFeature.h
engine/include/graphics/vulkan/UiDrawPass.h
```

`cmake/CheckUiIsolation.cmake` (ctest `ui_isolation`) enforces that boundary
across the engine, the editors, `app/`, `example/`, `templates/` and `test/`.

The line sits at `engine/src/ui/` rather than at `rml/` inside it, deliberately.
The runtime *is* the integration: its contexts and documents are RmlUi objects,
and an adapter hiding them would be an interface around one class with no
boundary behind it. What the guard protects is that nothing else -- above all
the installed public headers -- can name the layout engine.

That guard is load-bearing rather than tidy. `engine/include/` is installed
verbatim into the SDK, so an RmlUi include in one public header would oblige
Sencha to ship RmlUi's headers -- the position `imgui.h` is already in, and the
one this avoids repeating. The guard is also checked in the other direction:
`rmlui_core` is built with hidden visibility so its symbols never reach the
engine's dynamic symbol table, where a game module could bind to them and make
RmlUi part of the published surface by accident.

## 3. Build gating

Two switches, because most of the substrate needs no device.

| Switch | Covers |
|---|---|
| `SENCHA_ENABLE_UI` (ON) | `ui/` and `assets/ui/`: packages, documents, layout, models, actions. Depends on RmlUi and FreeType, **not** Vulkan. |
| `SENCHA_ENABLE_UI AND SENCHA_ENABLE_VULKAN` | `render/ui/`, `UiRenderFeature`, `UiDrawPass`, `engine/shaders/ui.*`. |

Without a render interface the runtime binds a recording no-op: documents still
load, lay out, and answer geometry queries. The UI tests exercise that path --
constructing the runtime with no graphics services -- which is what keeps the
split honest rather than aspirational.

One caveat worth knowing before relying on the switches. `SENCHA_ENABLE_UI=OFF`
is verified: the engine builds and RmlUi is absent from the link. A full
`SENCHA_ENABLE_VULKAN=OFF` configure, however, **does not build on `main`** and
has not for some time -- `RuntimeContent.cpp` reaches `vk_mem_alloc.h` through
`VulkanAllocatorService.h`, and `DefaultRenderPipeline.cpp:228` does not compile
with the backend filtered out. That breakage predates this work and is unrelated
to it. So the no-Vulkan configure is not available as a test venue today, and the
UI tests get their device-free coverage inside the ordinary build instead. The
source split is real either way, which is what the two filters buy.

RmlUi is pinned at **6.3** via `FetchContent`, built STATIC and linked PRIVATE.
That is the opposite of the `imgui` treatment in the same file, for the reason
recorded there in reverse: imgui is an OBJECT library because the editors link
only `sencha::engine` yet call ImGui, so those symbols must reach the export
table. Nothing outside `engine/src/ui/rml/` calls RmlUi, so it must not be
re-exported. `RMLUI_DIR` substitutes a vendored copy.

FreeType is RmlUi's font engine, found with `find_package(Freetype REQUIRED)`.
Implementing `FontEngineInterface` ourselves buys nothing we need.

## 4. Render phases

```cpp
enum class RenderPhase : std::uint8_t
{ Offscreen = 0, MainColor = 1, ApplicationUi = 2, DevelopmentOverlay = 3, Count };
```

`Renderer::RecordSwapchainPhases` records the last three inside one
`vkCmdBeginRendering` scope, in order, and an empty bucket is skipped entirely.
Capture and the present transition stay after the last one, so a frame capture is
still the finished frame. UI pipelines disable depth test and write; the depth
attachment stays bound, as it already was for ImGui.

**ApplicationUi is authored, user-facing UI. DevelopmentOverlay is diagnostics.**
The distinction is not stylistic, and it decides where an ImGui host belongs:

| Feature | Phase | Why |
|---|---|---|
| `EditorUiFeature` (Kyusu's shell) | `ApplicationUi` | It is the editor's *application UI*, which happens to be built in ImGui. Retained surfaces record later in the same phase, so an authored panel draws **over** the ImGui panel it replaces. Putting it in `DevelopmentOverlay` would let the legacy shell paint over its own replacement and make incremental migration impossible. |
| `ImGuiDebugOverlay` | `DevelopmentOverlay` | The console and timing panels, which must stay readable on top of whatever is underneath. |

Order within `ApplicationUi` is a **declared edge**, not a staging accident.
`UiRenderFeature` stages under a known id and accepts host-supplied predecessors,
so Kyusu names its ImGui shell as one and a game names nothing.

GPU scopes `Phase/ApplicationUi` and `Phase/DevelopmentOverlay` report each
bucket separately under `render.profile.mode gpu`.

## 5. Frame ordering

No new `FramePhase`. The existing twelve cover it.

| Phase | UI work |
|---|---|
| `PumpPlatform` (0) | The device snapshot is folded first; surfaces are then offered the event in z-order (§6). |
| `PreSimulate` | Unchanged. `ui.navigate_up`, `ui.accept`, `ui.cancel` resolve like any other mapped action, so they honour remapping and controller profiles. No document names a gamepad button. |
| `Simulate` (7) | RmlUi is not involved. Fixed simulation never reads document state. |
| `Update` (9) | Host controllers run first, draining actions and publishing model values; the engine then calls the UI runtime's update **explicitly**, applying dirty values, processing the screen stack and navigation, and laying out. An action taken this frame is visible this frame, by mechanism -- not by having registered a system later than somebody else. |
| `ExtractRender` (10) | The runtime renders against the recording adapter and publishes an immutable `UiDrawFrame`. No GPU work. |
| `Render` (11) | `UiRenderFeature::OnDraw`, in the `ApplicationUi` bucket. |

## 6. Input

**The canonical device snapshot is authoritative and unconditional.**
`PlatformEventRouter::Route` folds every event into the `InputFrame` before
offering it to any consumer. A consumer claiming an event hides it from later
consumers -- never from the snapshot.

This is a correction, not a preference. Routing consumers ahead of capture means
a surface that claims a key-up throws the release edge away and the key reads as
held for as long as that surface is open. The engine used to compensate with a
blanket `ReleaseAllHeld()` every frame the console was open, which cleared
genuinely-held gameplay keys along with the stuck one. `PlatformEventRouterTests`
covers both directions.

Routing order mirrors the z-order in §4: diagnostics, then authored UI, then the
application (and an editor's own shell).

A surface that consumed input **publishes** it in `InputFrame::UiCapture` rather
than hiding the events. Two kinds of reader care:

- **Mapped-action readers** ignore it. An `InputContextLease` already decides what
  they hear, and gameplay/editor-command suppression stays entirely that
  mechanism's job. The UI never decides which game controls cease to exist.
- **Raw `InputFrame` readers** gate on it, because the snapshot faithfully
  contains keystrokes typed into a console. The engine's own two (`ExitOnEscape`,
  `TogglePauseOnF1`) do. A game reading raw input should do the same or move to
  actions.

Text entry uses real platform text and IME events (`SDL_EVENT_TEXT_INPUT`,
`SDL_EVENT_TEXT_EDITING`, `SDL_StartTextInput`, `SDL_SetTextInputArea`), driven by
RmlUi's per-context `TextInputHandler`. Characters are never reconstructed from
keycodes. *(Stage 5.)*

## 7. Rendering capability profile

Taken from RmlUi 6.3's `RenderInterface.h` and its render-interface
documentation, not assumed. The distinction matters because the *required* set
alone does not buy the visuals authored chrome wants.

**Tier 1 -- implemented.** The eight required methods (`CompileGeometry`,
`RenderGeometry`, `ReleaseGeometry`, `LoadTexture`, `GenerateTexture`,
`ReleaseTexture`, `EnableScissorRegion`, `SetScissorRegion`), plus `SetTransform`,
plus `EnableClipMask` / `RenderToClipMask`.

The clip mask is Tier 1 deliberately: RmlUi needs it for `border-radius` combined
with `overflow`, and for `transform`/`perspective`. A rectangular scissor does not
clip to a rounded boundary, and rounded clipped panels are exactly what Kyusu's
chrome is made of.

**Tier 2 -- not implemented.** `PushLayer`/`CompositeLayers`/`PopLayer`,
`SaveLayerAsTexture`, `SaveLayerAsMaskImage`, `CompileFilter`, `CompileShader`.
The RCSS features that need them are **out of contract**: `box-shadow`,
`filter`/`backdrop-filter`, `mask-image`, and **gradient decorators**. Gradients
are authored as textures until Tier 2 lands.

**Two guards, not one.** The runtime adapter overrides every optional method with
a diagnostic stub that logs once per site and draws nothing, so an unsupported
operation that actually reaches the renderer fails loudly. The `.sui` cooker
additionally lints for known Tier-2 triggers at cook time. The cooker is a
convenience and not the authority: inferring every renderer requirement
statically would mean reimplementing part of RmlUi's cascade.

### Stencil

Clip masks need a stencil aspect. `VulkanDepthTarget::ChooseDepthFormat` today
prefers `VK_FORMAT_D32_SFLOAT`, which has none, falling back to
`D24_UNORM_S8_UINT` / `D32_SFLOAT_S8_UINT`. **Decision:** when UI rendering is
compiled in, prefer a stencil-bearing format; keep the stencil-free one for
builds without it. Depth precision is unchanged either way, so golden images are
unaffected. A device offering no stencil-bearing depth format degrades
`border-radius` clipping to rectangular scissor with a one-time diagnostic rather
than failing to start. Implemented and golden-tested in Stage 3.

## 8. Colour, alpha, and blend state

RmlUi hands over vertex colours **and** generated textures in **sRGB with
premultiplied alpha**, and expects `(ONE, ONE_MINUS_SRC_ALPHA)` blending. The
swapchain is `VK_FORMAT_B8G8R8A8_SRGB`, so the GPU encodes linear to sRGB on
write. Four rules follow, and they are the Stage 3 golden test's written
contract:

- **Vertex colours convert to linear before interpolation** -- in the vertex
  shader or at record time, never in the fragment shader. Interpolating sRGB
  values is wrong no matter what happens afterwards. Because they are
  premultiplied, the conversion is unpremultiply, convert, repremultiply.
  **Alpha is never converted.**
- **Generated textures are RGBA8 premultiplied sRGB**, not single-channel
  coverage. Sampling them through an `_SRGB` view would decode premultiplied
  values, which is also wrong, so they are converted once on the CPU at upload and
  uploaded as `R8G8B8A8_UNORM` holding linear premultiplied data.
- **Content textures** loaded as ordinary assets are straight-alpha sRGB: they
  sample through an `_SRGB` view and are premultiplied in the shader.
- **Blend state:** `srcColor = ONE`, `dstColor = ONE_MINUS_SRC_ALPHA`,
  `srcAlpha = ONE`, `dstAlpha = ONE_MINUS_SRC_ALPHA`.

The editor already carries the sRGB half of this lesson: `EditorUiStyle` stores
its whole palette in linear for the same reason.

## 9. Resources and ownership

**RmlUi creates presentation-domain resources; the renderer creates GPU
resources.** The runtime links no backend header and owns no GPU object.

- `GenerateTexture` mints a `UiTextureId` and stores CPU pixels. The record
  travels in the draw frame the first time it is used; the render feature
  materialises, owns, and retires the GPU image.
- `LoadTexture` resolves against the loaded package's **resource table** to an
  asset the open screen already holds a lease on. It performs no ad-hoc asset
  discovery: a source the table does not name is an authoring error, diagnosed as
  one, not fetched at render time.

A submitted `UiDrawFrame` is self-contained. It holds **references to immutable
CPU geometry blobs**, not handles into a mutable store, so the render feature
never reaches back into runtime state. CPU geometry lifetime follows reference
lifetime; `GpuFrameRetirement` is used only for GPU resources still named by
submitted command buffers, which is what it is for.

Dependency lifetime has one owner per layer: the package cache owns cooked
package data, the preloader warms declared dependencies, and an **open screen
instance** owns the asset leases its document needs. Document lifetime and
resource lifetime then line up exactly.

## 10. The content path

```
.rml + the .rcss files it imports
        |  UiPackageImporter (dev-only, SENCHA_ENABLE_COOK)
        v
     .sui  ---- declares ---->  Font / Texture assets
        |                        (referenced, never inlined)
        v
   AssetSystem  ->  UiPackageCache
        |
        v
   an open screen, which holds the leases
```

`.sui` carries the root markup, every stylesheet blob, the resource table, and
the constructs the cooker noticed are outside the profile. A package opens with
no filesystem beneath it -- which is why stylesheets are copied in, while fonts
and textures, being real assets with their own identity, are referenced.

Only the root `.rml` is a cooked asset. A `.rcss` has no runtime identity of its
own; a document that imports it carries a copy.

**Stylesheets are inputs to freshness, not just to the cook.** The importer reads
them through `ImportInput::Sources` and lists them in
`ImportResult::AdditionalSources`; the driver records them in the cooked index
and folds them into the fingerprint, so editing a shared theme recooks every
document that imports it. That is what `kCookedCacheIndexVersion` 8 is for.
Reading a sibling any other way would leave it outside the hash, and a stale
build cache is a worse failure than a missing feature because nothing about it
looks wrong.

Fonts cook `.ttf`/`.otf` to `.sfont`: the face bytes unchanged, plus the family,
style and weight to register them under, taken from the filename convention
(`Inter-BoldItalic.ttf`) and overridable in a `.meta` sidecar. No glyphs are
baked. An atlas depends on the size a document asks for and on the renderer that
samples it, so it belongs to the runtime that draws the text.

## 11. What does not belong here

Kyusu's spatial interaction stays purpose-built: transform gizmos, resize
handles, carve, clip planes and pins, face highlights, vertex/edge/face selection,
measurement, snapping feedback, viewport annotations, the tool wheel. These
operate on geometry and need viewport-aware hit testing, projection and
manipulation. Expressing them as documents would weaken them.

Dear ImGui keeps the work it is good at: the profiler, render statistics, ECS
inspection, memory and networking diagnostics, experimental controls, and any
surface an engineer wants to exist ten minutes from now. It stops being the
default substrate for surfaces meant to become part of Kyusu's designed
experience, and those migrate one at a time -- the ImGui version staying until the
replacement is better.

## 12. Related documents

| Doc | Relationship |
|---|---|
| `docs/plans/engine-roadmap.md` | Owns versions and sequencing. Track A item 9 is this work. |
| `docs/renderer/features-and-passes.md` | The phase and feature contract this plugs into. |
| `docs/assets/architecture.md` | The asset kind, staging, and lease machinery `.sui` and fonts use unchanged. |
| `docs/renderer/frame.md` | Where extraction and recording sit in the frame. |
