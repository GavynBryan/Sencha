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

Text entry uses real platform text and IME events. A focused field starts
platform text input and reports its caret box so the IME puts candidates
somewhere other than on top of what is being composed; losing focus, or the
field being destroyed under the caret, stops it. Characters are never
reconstructed from keycodes -- that is wrong in every locale but the author's.

Text input is started and stopped rather than left on, because a platform with
it always active is one where the IME is always eligible to eat a keystroke
meant for the game.

### Navigation

`UiService::Navigate(surface, UiNavigation::…)` — Up, Down, Left, Right, Next,
Previous, Accept, Cancel. The host maps its own actions onto these; the layer
translates them into the focus and spatial navigation the document engine
already implements. No document names a key or a gamepad button anywhere in that
path, so remapping, controller profiles and accessibility settings keep working
without knowing a document exists.

### Two authoring contracts worth knowing before they bite

**`pointer-events: none` on a full-screen root.** A document's `body` is
interactive by default, so a HUD covering the window counts as "the pointer is
over the UI" and quietly takes the mouse from the game. Interactive elements opt
back in with `pointer-events: auto`.

**`tab-index: auto` to be focusable.** An element without it is not in the tab
order, so navigation cannot reach it -- which means a controller and a keyboard
cannot either, however well the rest is wired.

Both are silent when missed, which is why they are written down here and
exercised in `UiInputTests.cpp`.

## 7. Rendering capability profile

Taken from RmlUi 6.3's `RenderInterface.h` and its render-interface
documentation, not assumed. The distinction matters because the *required* set
alone does not buy the visuals authored chrome wants.

**Tier 1.** The eight required methods (`CompileGeometry`, `RenderGeometry`,
`ReleaseGeometry`, `LoadTexture`, `GenerateTexture`, `ReleaseTexture`,
`EnableScissorRegion`, `SetScissorRegion`), plus `SetTransform`, plus
`EnableClipMask` / `RenderToClipMask`.

The clip mask is Tier 1 rather than Tier 2 deliberately: RmlUi needs it for
`border-radius` combined with `overflow`, and for `transform`/`perspective`, and
a rectangular scissor cannot express any of that -- while rounded panels are
most of what authored chrome is made of.

It is stencil-backed, following the semantics RmlUi's own backends use. `Set`
clears the stencil to zero and stamps one over the mask geometry; `SetInverse`
clears to one and stamps zero, so what passes is the area *outside*; `Intersect`
increments and raises the test reference, so a nested clip passes only where
every enclosing mask also covered. Four pipeline variants cover it, because
whether a draw writes the mask or tests against it is pipeline state: reference,
compare mask and write mask stay dynamic so nesting depth does not multiply
pipelines.

A device with no stencil-bearing depth format skips the mask writes, leaving the
draws it would have clipped unclipped, and says so once.

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

Clip masks need a stencil aspect. `VulkanDepthTarget::ChooseDepthFormat` now
prefers `VK_FORMAT_D32_SFLOAT_S8_UINT`, then `D32_SFLOAT`, then
`D24_UNORM_S8_UINT` -- 32-bit float depth stays the first choice either way,
because depth precision is not a thing to trade for a stencil aspect. Verified
against the golden images: the format change moved no pixel.

The whole swapchain scope binds that stencil aspect, so **every pipeline
recording into it declares the format** -- the mesh and sky passes, and both
ImGui hosts. Dynamic rendering matches attachment formats, not intentions: a
pipeline that never touches the stencil still has to say so. Verified against
the golden images, which did not move.

A device offering no stencil-bearing depth format degrades `border-radius`
clipping to a rectangle with a one-time diagnostic rather than failing to
start.

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

That makes shutdown order a real contract rather than a detail. A host must call
`UiService::Shutdown()` while the asset caches are still alive -- from
`Game::OnShutdown`, not from wherever its `UiService` member happens to be
destroyed. A lease outliving the cache it references calls `Detach` on a
destroyed owner, which is a pure-virtual call at exit that points nowhere near
its cause.

## 10. Presentation models and semantic actions

A screen declares both when it opens, because the document engine binds a model
before it parses the document that reads it -- and an action the host never
declared is one it has no code to handle, better refused at open than dispatched
at runtime.

```cpp
UiScreenDesc desc;
desc.PackagePath = "asset://ui/hud.rml";
desc.ModelName   = "hud";
desc.Properties  = { { "health", UiValue(100.0) }, { "weapon", UiValue("none") } };
desc.Actions     = { "pause_quit" };
```

Ids are positional (`UiPropertyIdAt(0)`), so a host names them as constants
beside the description that declares them and never parses a path per frame.
`FindProperty`/`FindAction` resolve by name once for a host that would rather.

**Setting compares before it dirties.** A value equal to the one already there
marks nothing, so a HUD may publish every frame without re-evaluating every
binding that reads the property. `SetValue` returns whether anything changed.

**Actions carry copies and nothing else.** No element pointer, no DOM node, no
entity, no editor object, no callback the document registered. An action says
what was asked; turning `inspector.set_position` into a validated, undoable
command is the controller's job, which is what keeps undo, transactions and
scripting outside the presentation layer.

**Names must be identifiers.** A data expression reads `.` as member access, so
`pause.quit` is not a callback of that name -- it is the member `quit` of
something called `pause`, and binding it silently does nothing. The runtime
refuses a name it cannot bind, with the reason, at open. Use `pause_quit`.

Geometry bound from a model goes through `data-style-width` and friends, not an
interpolated `style=""`: the engine substitutes data expressions in text and in
`data-*` attributes only.

**Two-way editing, without a control ever becoming authoritative.** A property
declared `Editable` gets a setter, so a bound control -- a text field, a slider,
a checkbox -- can write it. That write changes the *presentation copy* and
nothing else. It does not reach the application and it is not a commit: a value
someone is part-way through typing is presentation state, exactly like a scroll
offset.

The host learns what was typed by reading it back when the document raises the
action that says to -- an apply, a preview, a confirm -- and stays free to
validate it, transform it, or refuse it. That is what keeps undo, transactions,
validation and scripting on the far side of an explicit action. It is also why
cancelling and interruption are free: closing a screen mid-edit, or destroying
its surface, commits nothing, because the edit never went anywhere.

A property without `Editable` has no setter bound at all, so the engine refuses
the write rather than accepting it into a value nothing reads back.

**Lists** are declared separately (`UiScreenDesc::Arrays`) because the engine
binds an array by address rather than through the value getter. They are lists
of strings, deliberately: a presentation list is labels -- profile names, asset
paths, search results -- and a row needing more structure than that is a design
question rather than a missing overload. Same compare-then-dirty rule, so a
panel republishing its list every frame re-runs nothing.

**Rows** (`UiScreenDesc::RowLists`) are the shape most editor surfaces actually
present: an inspector's fields, a property sheet, a result set. A `UiRow` is a
label, a value and an optional detail -- strings, like a list, because
presentation is text and a number being edited is the text somebody is typing.
The host parses it when it reads the row back, which is also where it gets to
refuse "1.2.3" without the document ever having had an opinion.

A row's members are bound by pointer-to-member, so each is read-write: a control
inside a repeated row edits the presentation copy directly, with no setter per
row, and the value still goes no further than that copy until an action says to
read it. `UiRow::Editable` says whether the document should *offer* a control at
all -- an identity, an asset handle, a leaf the schema cannot express. It is
presentation metadata rather than a gate, because a struct member binds once for
the whole array and not once per element; a document that offers a control
anyway still only writes the copy, and the host is still what decides whether a
value read back becomes a change.

Arbitrary value structs are still not here. Rows cover what surfaces have asked
for; a shape rows cannot express is a design question when it turns up.

**Actions drain per screen.** `DrainActions(screen)` is what a controller that
owns a screen calls. The no-argument overload takes every screen's, which is
right for a host that owns them all and wrong the moment a second controller
exists: whichever ran first would swallow the other's actions, and the other
would simply never hear what its document asked for.

### The engine drives it

`Engine::Ui()` from `Game::OnStart` onward. The engine calls the runtime's update
inside `FramePhase::Update` **immediately after** dispatching the game's
frame-update systems -- explicitly, not by registering a system that happens to
sort later. Host controllers drain actions, change state and republish values in
their own systems; the engine then applies what they published and lays out. That
is what makes an action taken this frame visible in this frame, and it is a
guarantee the engine owes rather than one a module could arrange for itself.

Extraction follows in `ExtractRender`, and the engine stages the render feature
for every host, so a game gets authored UI drawing without assembling anything.

## 11. The content path

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
style and weight, taken from the filename convention (`Inter-BoldItalic.ttf`)
and overridable in a `.meta` sidecar. No glyphs are baked. An atlas depends on
the size a document asks for and on the renderer that samples it, so it belongs
to the runtime that draws the text.

A document declares the faces it uses in RCSS, with `@font-face`, and the
document engine loads them through the file interface like any other resource --
the runtime answers that request with the cooked bytes the open screen leases.
Registering them a second time from the cooked metadata would be a competing
source of truth for what a face is called, so the runtime does not; the `.sfont`
metadata is the default for registering a face programmatically instead.

## 12. The authoring loop

```
edit .rml / .rcss
        |  source watcher
        v
  is it an asset?  ── no ──>  which cooks recorded it as an input?
        | yes                          |  (CookedSourceEntry::AdditionalSources)
        v                              v
   re-cook it  <───────────────  re-cook each of those
        v
   .sui reload  ->  UiPackageCache::ReloadInPlace  (slot, handle, refcount kept)
        v
   open screens rebuild
```

A `.rcss` is not an asset -- it has no runtime identity, and a document that
imports it carries a copy. So editing one reaches the runtime the same way it
reaches the cook: by asking which cooks recorded it as an input. The index
already knows, because the cook had to record it to get freshness right (§11),
so this is a lookup rather than a guess. Without it, editing a shared stylesheet
would watch the file, notice the change, and do nothing.

**What survives a rebuild** is the point, not the rebuild itself:

- the presentation model -- every value and list the host published, so a HUD
  does not forget the health it was showing because somebody moved a margin;
- the screen handle and every id resolved from it, so a host that resolved its
  properties once at open does not start addressing the wrong ones;
- the screen's place in its surface, its modal flag, and the leases it holds.

Focus and scroll are **not** carried yet. They are cheap to add and were left
until a surface complains, which is the same rule the rest of this followed.

The document engine caches parsed stylesheets, templates and font faces by name,
so all three are dropped before a rebuild -- otherwise it answers from what it
parsed the first time and the edit never appears. Font faces are keyed by family
rather than by the asset they came from, so there is no way to drop one without
dropping the set; a rebuild immediately re-requests each through the file
interface, which answers from the leases the screens hold.

**The previous-valid-document guarantee comes from the cook, not from here.** A
source that fails to cook produces no artifact, so the package in the cache is
untouched and no reload is triggered: the document already open keeps running
and the author sees the cook error. What this layer handles is the rarer case of
a package that cooked but could not construct, which leaves the screen open and
empty with the reason logged, once -- the version is stamped before the attempt
so a broken document is not retried at frame rate.

Kyusu watches its own UI root, so editing the editor's interface while the editor
is running is a save-and-look loop rather than a restart.

## 13. Rendering destinations

**One surface per host surface, not per controller.** Focus and modality are
arbitrated within a surface, so a dialog opened on a surface of its own would
take focus from nothing while the screens it meant to block kept taking clicks.
Kyusu creates one surface for its window and hands it to every authored
controller; the surface tracks the window's size each frame, because a retained
document re-flows on a resize where a baked font atlas cannot.

Today a `UiSurface` renders into the window's swapchain, in the `ApplicationUi`
phase. That is the whole of what exists, and it is enough for a game's HUD and
menus and for an editor dialog that covers the window.

It is **not** enough for an authored surface that has to occupy part of an
editor's layout. Kyusu composites its viewports by rendering to an offscreen
target and handing the result to ImGui as a texture; an authored panel sitting
in a dock would need the same, and `UiDrawPass` has no notion of a destination
other than the swapchain scope.

The generalisation, when it is earned:

```
UiSurface
    ├── target: Window / Swapchain
    └── target: RenderTarget
```

`UiDrawPass` should be given a render destination by the host or the render
graph. It should learn nothing about docking, panels, or ImGui -- those are one
consumer of a texture during a migration, not concepts the retained UI
architecture should carry. The same machinery then serves a window, an offscreen
editor target, a second viewport, or whatever comes next.

Deliberately deferred to the panel-reduction work rather than built to unblock
the first migration. Building it first would have turned "prove Kyusu can use
authored UI" into "extend the renderer until authored UI can reproduce Kyusu's
current docking architecture", which is a different project and not a
prerequisite for the first. And by the time it exists, some surfaces will turn
out not to want a docked successor at all -- project configuration among them.

## 14. What does not belong here

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

## 15. Related documents

| Doc | Relationship |
|---|---|
| `docs/plans/engine-roadmap.md` | Owns versions and sequencing. Track A item 9 is this work. |
| `docs/renderer/features-and-passes.md` | The phase and feature contract this plugs into. |
| `docs/assets/architecture.md` | The asset kind, staging, and lease machinery `.sui` and fonts use unchanged. |
| `docs/renderer/frame.md` | Where extraction and recording sit in the frame. |
