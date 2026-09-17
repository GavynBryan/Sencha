# Runtime UI Execution Plan

Status: proposed execution specification, 2026-07-22.

This document owns the implementation shape for Sencha's game-facing runtime UI. It supersedes the implementation detail in `docs/plans/engine-roadmap.md` Track A item 9. The roadmap continues to own version assignment and release gates.

The intended authoring model is Panorama-like:

- RML-style markup owns document structure.
- RCSS-style stylesheets own layout and presentation.
- Native game code owns application behavior through presentation models and semantic actions.
- A future scripting runtime may provide UI controllers through the same action and model surfaces. It does not change the UI ownership model.

This is not a plan to embed a browser, adopt JavaScript, store UI nodes in the gameplay ECS, or replace Dear ImGui in the editors.

---

## 1. Product outcome

A game project can author a HUD, title screen, pause menu, settings screen, save list, and modal prompt as cooked assets without rebuilding Sencha. The runtime:

- loads markup and styles through the normal asset pipeline;
- lays out and updates a retained document tree at presentation rate;
- routes keyboard, pointer, text, and controller input with real focus and capture semantics;
- renders immutable UI draw data through Sencha's renderer without traversing live UI or ECS state from the Vulkan backend;
- receives copied presentation values from game systems;
- emits semantic action events that game code handles at an explicit frame boundary;
- hot-reloads source markup and styles in development while preserving compatible interaction state;
- remains available in shipping configurations where debug UI is compiled out.

The first complete vertical slice is not a static health bar. It is a small interactive screen set containing:

1. an ECS-backed HUD;
2. a pause menu with keyboard and pointer navigation;
3. one settings control with preview and commit behavior;
4. a modal confirmation prompt;
5. a development hot-reload path;
6. a shipping build with Dear ImGui disabled.

This cut forces the architecture to prove data flow, input ownership, focus, actions, text, asset loading, rendering, lifecycle, and configuration. A quad-only HUD would allow most of the difficult contracts to remain undefined.

---

## 2. Verified starting state

The plan is based on the current tree, not earlier UI proposals.

### 2.1 Frame and system ownership

- `Engine` is the integration root.
- `FrameDriver` owns the fixed ten-phase frame pipeline.
- `EngineSchedule` runs game and engine systems in fixed, physics, post-fixed, frame-update, render-extract, audio, and end-frame phases.
- `FrameUpdateContext` already identifies HUD animation and menu timers as presentation-rate work.
- `RenderPacket` is the simulation and presentation to renderer handoff.
- `Renderer` owns ordered `IRenderFeature` instances and never reads live ECS state.

### 2.2 Input

- `FrameDriver` currently owns `InputFrame`.
- `SdlInputCapture` folds SDL events into held state and edge lists.
- `InputFrame` is a gameplay snapshot. It does not retain event order, text input, composition state, modifiers per event, pointer capture, or consumption ownership.
- `RegisterDefaultEngineFramePhases` currently calls `SdlInputCapture::Accept` before `Game::OnPlatformEvent`, so a game callback marking an event handled cannot prevent that event from entering gameplay input.
- The current code therefore disagrees with the intended documentation contract that application event handling precedes default input capture.
- Dear ImGui event processing is wired by each game through `Game::OnPlatformEvent`.

`InputFrame` must remain a compact simulation-facing snapshot. Expanding it until it resembles a DOM event queue would merge two different mechanisms and still leave consumption and focus ownership ambiguous.

### 2.3 Rendering

- `RenderPhase` currently has `Offscreen` and `MainColor`.
- The renderer records every `MainColor` feature inside one dynamic rendering scope.
- The phase enum explicitly reserves future UI and post-processing phases.
- `ImGuiDebugOverlay` is a normal render feature and currently reports `MainColor`.
- `RenderPacket` currently contains camera and world render queue data only.

Runtime UI must gain a named render-domain handoff and render phase. It must not call into a live document tree from `UiRenderFeature::OnDraw`.

### 2.4 Assets

- Runtime formats enter through `AssetRegistry`, `IAssetLoader`, `AssetSystem`, and the staged load/owner-thread commit contract.
- `RuntimeAssets` and `AssetSystem` are currently extended by adding constructor parameters, cache members, typed methods, and central `AssetType` switches.
- `AssetPreloader` owns separate vectors per handle type and repeats type switches for acquisition, commit, delivery, and release.
- Material dependency ordering is hardcoded as a two-wave special case.
- New UI package and font asset types would deepen those branch piles and require another dependency special case.

The UI work must not add two more locally contained asset paths. It must first make the existing asset front door able to metabolize additional runtime asset types through one registered operation record per type.

### 2.5 ECS

- ECS storage is registry-local, archetype-based, and structurally mutable only through declared command boundaries.
- Components cannot own relocatable runtime resources.
- Render extraction copies values out of ECS.
- Global and zone registries have explicit participation views.

A UI document is not a registry, zone, entity hierarchy, or component family. It is a retained presentation object with parent-dependent layout, ordered input routing, focus, capture, text selection, and paint invalidation. The gameplay ECS supplies presentation values and consumes actions. It does not own the UI tree.

### 2.6 Module ABI

- The engine builds a fingerprint from a selected header set.
- Public app contexts expose `InputFrame` and `RenderPacket` through included headers.
- The current fingerprint list is not a transitive closure of every layout that game modules can compile against.

The UI work adds public service access and may extend render-domain records. ABI coverage must be corrected before those changes land. A new UI feature must not rely on an incomplete skew detector.

---

## 3. Invariant and owner

The invariant is:

> A runtime UI document has one owner, consumes complete ordered input through one routing mechanism, observes game state only through copied presentation models, emits only semantic actions, and hands immutable paint data to the renderer.

Ownership is divided by mechanism:

- `InputRouter` owns ordered input dispatch and the terminal gameplay `InputFrame` snapshot.
- `UiRuntime` owns RmlUi process lifetime, UI contexts, loaded documents, focus, capture, presentation models, action queues, and CPU-side paint extraction.
- `UiPackageCache` and `FontCache` own resident cooked UI and font assets.
- `UiRenderFeature` owns Vulkan pipelines, generated GPU textures, and per-frame UI draw submission.
- Game systems own the derivation of presentation values from ECS state.
- Game systems or application code own the meaning of emitted UI actions.
- `RenderPacket` owns the immutable UI render data for one frame.

No second owner mirrors canonical state. A UI property that displays health is a copied presentation value, not a second health authority. A settings control may hold transient preview state, but committed configuration remains owned by the configuration system.

---

## 4. Pinned decisions

These decisions are binding for the first implementation unless owner review explicitly changes them.

### D1. RmlUi is the retained document kernel

Use RmlUi as the markup parser, style engine, layout engine, element tree, event system, and built-in control substrate. Pin an exact reviewed release in CMake. At the time of this plan, release 6.2 is the candidate.

Do not copy RmlUi's sample backend into general engine code and rename it. Implement Sencha-owned platform, file, system, text-input, and render adapters around Sencha's existing boundaries.

### D2. RmlUi is a private dependency

No public engine header includes an RmlUi header or exposes an RmlUi type. `UiRuntime` uses a private implementation. Add a dependency fitness test that permits `RmlUi/` includes only in the dedicated adapter subtree and build files.

Disable the Lua plugin, sample applications, and unrelated plugins in the engine build. A future scripting integration must use Sencha's scripting runtime and the public UI action/model API.

### D3. Runtime UI lives beside ECS, not inside it

`UiRuntime` is engine-owned presentation infrastructure. Documents and model values may be game-owned through handles, but UI nodes are not entities and are never stored in a gameplay `Registry`.

A future world-space UI surface may use an ECS component containing document and render-surface handles. The document tree remains owned by `UiRuntime`. World-space surfaces are out of scope until screen-space UI ships.

### D4. Dear ImGui remains debug and editor UI

Do not migrate existing editor panels, debug overlays, console panels, or timing panels to runtime UI as part of this work. Runtime UI and ImGui share platform and renderer ordering only. Runtime UI must build and run with `SENCHA_ENABLE_DEBUG_UI=OFF`.

### D5. There is no JavaScript or inline arbitrary script

Markup may declare semantic actions and data expressions. It may not call arbitrary engine methods, name C++ symbols, evaluate JavaScript, or hold gameplay logic.

The initial controller surface is:

- copied model values into the document;
- semantic action attributes on elements;
- action events queued out of the document;
- native custom elements registered at engine startup when composition is insufficient.

### D6. RmlUi data binding stays behind the adapter

RmlUi's native model API may bind raw pointers and has dynamic-library registration caveats. Game modules never bind their component pointers or arbitrary object addresses directly into RmlUi.

Sencha owns stable model storage. The adapter binds RmlUi getter and setter functions to that storage. Document reload, game-module unload, registry relocation, and ECS structural changes therefore cannot leave RmlUi holding game-owned addresses.

### D7. Presentation models are one-way by default

A game presenter copies values into UI model storage during `FrameUpdate`. Controls that edit application state emit preview, commit, or cancel actions. Broad automatic two-way binding is not part of the public contract.

This keeps validation, configuration persistence, undo where applicable, replication policy, and side effects in the owning system.

### D8. UI actions are deferred semantic events

An element declares an authored action name. At package load, the name resolves to a document-local dense action index backed by stable authoring text. Runtime input queues a `UiActionEvent` containing document identity, action identity, source identity when available, and a bounded typed payload.

The event listener does not mutate ECS, load assets, rebuild documents, or call game code from inside RmlUi traversal. Game code drains actions at a declared presentation boundary. ECS structural changes use `CommandBuffer`.

### D9. UI input has a dedicated complete event path

Add a platform-agnostic `InputEvent` domain value that preserves the complete information needed by consumers:

- event kind and ordering;
- key and repeat state;
- modifiers at the time of the event;
- text input and text-edit composition;
- pointer position, delta, buttons, wheel, and pointer identity;
- controller button and axis data;
- focus and relevant window identity.

SDL translation happens once. Consumers receive the whole event. They do not receive decomposed subsets that can silently drop fields.

### D10. `InputRouter` owns event consumption and `InputFrame`

Move terminal gameplay snapshot ownership out of `FrameDriver` into `InputRouter`. `FrameDriver` receives the current snapshot through its phase context and preserves the existing zero-tick edge behavior.

Routes are ordered by declared layer, not registration accident:

1. topmost development overlay, when present;
2. modal and ordinary runtime UI;
3. application-specific input route;
4. terminal gameplay snapshot capture.

Window lifecycle observation is not suppressed by input consumption. A consumed Escape key does not enter gameplay or trigger `ExitOnEscape`. A consumed pointer press does not activate relative mouse capture.

The exact registration API must use lifetime-bound tokens or explicit teardown. It may not leave a callback into a destroyed overlay or game object.

### D11. Existing raw platform events remain a separate concern

`Game::OnPlatformEvent` remains available for uncommon platform and window events during the transition. Ordinary keyboard, pointer, text, and controller behavior moves to the complete `InputEvent` route. Do not use the raw SDL callback as the runtime UI bridge.

If maintaining both callbacks creates duplicate input ownership, stop and complete the migration rather than adding synchronization flags.

### D12. UI updates after game presenters and before render extraction

The engine frame path calls game `FrameUpdate` systems first. `UiRuntime::Update` then applies model dirtiness, advances document animation using presentation time, resolves layout, and processes queued document work.

During `ExtractRenderPacket`, `UiRuntime` records immutable UI draw data after game render extraction. This order guarantees that a model value written this frame can appear in the same rendered frame.

The UI update is engine frame infrastructure, not a game system that happens to register in a favorable order.

### D13. The renderer consumes a packet, not a live document

Extend the render handoff with `UiRenderData`. `UiRenderFeature::OnDraw` reads only that data and renderer-owned caches. It never calls `Rml::Context::Update`, `Rml::Context::Render`, DOM APIs, model APIs, or ECS APIs.

The packet is safe against document mutation after extraction and preserves the option to move renderer consumption to another thread later.

### D14. Runtime UI has its own render phase

Add an explicit runtime UI phase after scene/post content and before the topmost debug overlay. Do not implement UI by pretending it is a transparent world material or by appending it to an arbitrary mesh feature.

If post-processing has not landed, the phase still exists in the correct final order. Do not make runtime UI depend on the future transparency pass described by the old roadmap text.

### D15. The first backend supports a declared RCSS profile

The first Vulkan backend supports:

- untextured and textured vertex geometry;
- per-vertex color;
- premultiplied alpha blending;
- rectangular scissor clipping;
- 2D transforms;
- external Sencha textures;
- generated RmlUi textures, including glyph atlases.

The cook validates a named Sencha runtime UI profile. Properties that require unimplemented clip masks, render layers, filters, custom shaders, or render-to-texture fail validation with a source location. They do not render incorrectly in silence.

The profile grows only when the backend and its tests grow with it.

### D16. UI source is cooked into one runtime package format

Authored `.rml` and `.rcss` files are source inputs. Shipping runtime loads a cooked `.sui` package.

A package contains:

- a versioned deterministic header;
- the root document and package-relative included markup/styles;
- a normalized virtual file table for RmlUi's file interface;
- an explicit dependency table for textures, fonts, localization tables, and nested UI packages if later allowed;
- validation metadata sufficient to report authored file and line on load failure;
- stable authored element ids required for hot-reload state transfer.

Do not serialize RmlUi internal objects or assume its in-memory representation is stable across library releases. Runtime may parse validated packaged RML/RCSS once when the asset commits. Parsing never occurs in the per-frame hot path.

### D17. RmlUi never reads the process working directory

Install a custom file interface before RmlUi initialization. It reads only from resident package files and explicitly mounted engine resources. Absolute paths and arbitrary relative disk reads are rejected.

This keeps packaged games, editor preview, hot reload, and tests on one content path.

### D18. Fonts are first-class runtime assets

Add a cooked font asset and cache rather than embedding duplicate font bytes in each UI package or reading loose TTF/OTF files at runtime. UI packages reference fonts through asset paths.

The first implementation may use RmlUi's FreeType font engine behind the adapter. Font bytes and registration metadata still flow through Sencha's asset system. Full fallback and localization policy remain owned by the localization track, but the initial API must permit fallback faces without changing package format.

### D19. External images reuse `TextureCache`

RML image and decorator references resolve to existing texture assets and `TextureHandle`s. There is no second PNG decoder, second image cache, or RmlUi TGA-only side path.

Generated UI textures such as glyph atlases are not authored assets. They live in a UI-generated texture cache owned by the render integration and are destroyed through the normal owner-thread GPU lifecycle.

### D20. Asset integration is corrected before adding UI types

Replace the current repeated `AssetType` branch piles with a fixed per-type runtime operation table. Existing concrete typed load methods remain readable public conveniences, but shared mechanisms use registered operations for:

- staged loader lookup;
- resident lookup;
- type-erased preload lease acquisition;
- staged commit;
- lease release;
- dependency class or dependency records where ordering is required.

`AssetPreload` stores one collection of owning type-erased leases instead of one vector per handle type. `AssetPreloader` no longer grows acquisition, commit, delivery, and release switches for every new asset type.

This refactor must migrate existing mesh, texture, material, audio, skeleton, animation, and skinned-mesh behavior before `UiPackage` and `Font` register. Do not leave an old path and a UI-only new path running in parallel.

### D21. Dependency ordering becomes data, not another wave branch

The current texture-then-material wave is the first dependency case. UI packages add fonts and textures. Animation adds skeleton dependencies. This is already a real variation axis.

Staged or cooked asset metadata declares dependencies. The preload coordinator commits an asset only after required dependencies are resident or have failed. Ordering is deterministic. Cycles and missing dependencies produce bounded diagnostics and fail the affected asset without deadlocking the preload.

The synchronous typed load path may retain narrow recursive convenience behavior temporarily, but async preload has one dependency mechanism. Do not add `if UI then wave 3`.

### D22. ABI coverage is repaired before public UI API lands

Create one authoritative ABI header manifest used by the fingerprint build, module build, and ABI fitness tests. It includes every public layout or inline implementation that crosses the game-module boundary, including transitive runtime, input, render packet, strong-id, and UI API records.

Add layout coverage for new POD-like boundary records. A UI API change that alters the module contract must produce a new fingerprint automatically.

Do not work around this by keeping unsafe UI state behind an untracked header.

### D23. Public UI values are bounded and explicit

The first public model API uses typed setters and stable handles rather than exposing an open `std::any`, arbitrary object pointer, or RmlUi variant across the game-module boundary.

Initial scalar types:

- boolean;
- signed integer;
- double precision number;
- owned UTF-8 text;
- color;
- asset reference or texture handle where explicitly supported.

Lists use immutable owned snapshots with explicit replacement and bounded size. They do not bind directly to a game `std::vector`.

### D24. Document and element identity are stable

Use strong generational ids for runtime contexts and documents. Authored element `id` values remain strings in source and compile to package-local stable identities. Runtime indices never serialize.

Hot reload uses authored ids plus compatible element type to restore focus, scroll position, selection, checked state, and other explicitly supported state. Missing or incompatible nodes fall back deterministically.

### D25. Hot reload keeps the last good document

Source change follows the existing cook and async publication path:

1. detect source change;
2. recook the package using the same importer as startup;
3. stage and validate off-thread where safe;
4. commit on the owner thread;
5. rebuild affected documents;
6. transfer compatible state;
7. publish the replacement atomically.

A failed cook or rebuild leaves the last good package and documents active. Diagnostics name the source file, location, document, and retained old version.

### D26. Headless UI core is supported

RML/RCSS package validation, model binding, action dispatch, layout, and paint recording must be testable without SDL video or Vulkan startup. The Vulkan feature is optional. Headless engine configurations may create UI contexts for tests and server-side document validation but submit no GPU work.

### D27. No editor replacement in this plan

Kyusu, Shudei, Kettle, and debug panels continue using ImGui. A later UI package preview panel may host a runtime UI context in an editor render target. It consumes the same cooked package and runtime interfaces. It does not fork the UI implementation or start a migration of editor chrome.

### D28. No speculative native widget hierarchy

Use RmlUi built-in controls and document composition first. Add a native custom element only when a shipping control requires behavior or rendering that composition cannot express. Each custom element owns a narrow mechanism and focused tests. Do not create `Widget`, `Control`, and `Panel` C++ inheritance trees beside RmlUi.

---

## 5. Target ownership and dependency shape

```text
Application / game module
  FrameUpdate presenter systems
    const reads from FrameRegistryView
    UiModelWriter typed writes
  UI action consumer
    drains UiActionEvent values
    issues session changes or CommandBuffer operations

Engine
  InputRouter
    InputEvent routes
    terminal InputFrame snapshot
  UiRuntime
    private RmlUi implementation
    contexts and documents
    model storage
    action queues
    package/file/system/text adapters
    CPU paint extraction
  FrameDriver
    fixed phase contract
    receives InputFrame from InputRouter
    owns RenderPacket double buffer
  RuntimeAssets or game-owned asset aggregate
    AssetRegistry
    asset operation table
    UiPackageCache
    FontCache
    TextureCache

Render domain
  RenderPacket
    world render data
    UiRenderData

Graphics/Vulkan
  UiRenderFeature
    runtime UI phase
    pipelines and descriptors
    generated texture uploads
    packet-only draw submission
  ImGuiDebugOverlay
    topmost development overlay phase
```

Dependency direction:

```text
ui core -> input domain, time, asset handles, math, logging
ui RmlUi adapter -> ui core + private RmlUi
ui asset loaders -> core asset contracts + ui package values
ui render domain -> ui plain draw values + texture handles
ui Vulkan feature -> ui render domain + graphics/vulkan
app integration -> ui runtime + frame driver + assets + renderer
world/ECS -> no dependency on ui
editor -> may depend on public ui runtime later
```

Forbidden directions:

- `ui/` does not include `ecs/`, `world/registry/`, gameplay framework, or game headers.
- RmlUi adapter code does not include Vulkan headers.
- Vulkan UI code does not include RmlUi, ECS, or game headers.
- UI package cook code does not link into shipping builds.
- Game presentation code does not receive RmlUi elements or DOM pointers.

Add fitness tests for these directions instead of relying on review.

---

## 6. Frame path

### PumpPlatform

1. SDL events are observed by the window service for lifecycle state.
2. Input-relevant SDL events translate once into complete `InputEvent` values.
3. `InputRouter` dispatches each event through ordered layers.
4. The runtime UI route updates focus, hover, capture, text composition, scrolling, and control state.
5. Unconsumed events reach the gameplay snapshot sink.
6. Raw platform events that are not represented by `InputEvent` remain available to the application platform hook.
7. Focus loss cancels pointer capture and active text composition through explicit termination paths.

### ScheduleTicks and Simulate

Gameplay receives only unconsumed `InputFrame` state. Existing edge preservation and first-tick drain semantics remain unchanged.

UI state is not advanced from fixed simulation time.

### Update

1. Game frame-update systems copy current presentation values into UI models.
2. Game code drains prior UI actions and applies session or configuration behavior. Structural ECS changes go through command buffers.
3. `UiRuntime::Update` applies model dirty state, advances transitions and animations using presentation time, performs layout, and finalizes actions produced by control updates.
4. Audio and other presentation systems continue according to their existing ordering.

The one-frame boundary for actions produced during this update is documented and deterministic. If a later shipping requirement needs an action before fixed simulation, add a named pre-simulation session action path. Do not let arbitrary UI callbacks execute during platform dispatch.

### ExtractRenderPacket

1. World transforms and world render data extract as today.
2. `UiRuntime` asks RmlUi to emit paint operations into a recording render interface.
3. The recorder writes `UiRenderData` into the current packet.
4. Packet data retains or copies every geometry and texture reference required until the packet slot resets.

### Render

1. Scene, transparency, and post features run in declared order.
2. `UiRenderFeature` consumes `UiRenderData`.
3. `ImGuiDebugOverlay`, when compiled and active, renders after runtime UI.
4. Present proceeds normally.

### EndFrame

- packet-held UI references release when their slot resets;
- action queues and input routes perform bounded cleanup;
- no document or GPU resource is destroyed from a worker thread.

Lifecycle-only frames continue pumping and terminating input. UI update and extraction follow the same renderability policy as other presentation systems. Minimize, resize, swapchain rebuild, and focus loss must not strand capture or text composition.

---

## 7. UI package and font formats

### 7.1 Source layout

Recommended project shape:

```text
assets/ui/hud/main.rml
assets/ui/hud/main.rcss
assets/ui/shared/theme.rcss
assets/fonts/inter_regular.ttf
```

The importer entry point is the root `.rml`. It resolves package-relative includes and explicitly declared external asset references. A stylesheet alone is not registered as a runtime asset.

### 7.2 Cooked package

Use a versioned chunked or length-delimited binary format. The exact encoding may follow existing binary asset conventions, but it must provide:

- magic and format version;
- deterministic path-sorted virtual file records;
- UTF-8 validation;
- content hashes;
- dependency records with stable `AssetId` plus path fallback when available;
- source-map records for diagnostics;
- profile version identifying the allowed element and property set.

Unknown optional chunks are preserved or skipped according to the existing cooked-format policy. Unknown required chunks fail clearly. Package output is byte-deterministic for identical source inputs.

### 7.3 Validation

Cook-time validation includes:

- XML/RML well-formedness;
- unique authored ids where state restoration or explicit targeting requires uniqueness;
- stylesheet parse and selector validation;
- unsupported render feature detection;
- unresolved includes;
- unresolved texture and font references;
- action name syntax;
- model expression syntax;
- package-relative path escape attempts;
- dependency cycles;
- limits on document depth, selector count, text size, generated list size metadata, and package bytes.

Runtime repeats safety-critical bounds checks because cooked files are untrusted input. It does not repeat full source authoring analysis every frame.

### 7.4 Fonts

The font cook stores original font bytes or a lossless shipping representation plus explicit face metadata. Do not rasterize a fixed-size atlas in the cook because UI scale, locale, and glyph demand are runtime concerns.

`FontCache` owns loaded face bytes and registration lifetime. `UiRuntime` registers retained font faces with the font engine and releases them only after no context or document can reference them.

---

## 8. Model and action contract

### 8.1 Model schemas

A model schema is registered before a document instance binds to it. Authoring names resolve once to dense property ids. Per-frame writes use handles, not string lookup.

Conceptual use:

```cpp
UiModelHandle hudModel = ui.CreateModel(hudSchema);
UiPropertyHandle health = hudSchema.Resolve("health");
UiPropertyHandle ammo = hudSchema.Resolve("ammo");

void HudPresenter::FrameUpdate(FrameUpdateContext& ctx)
{
    const HealthSnapshot snapshot = ReadHealth(ctx.Registries, Player);
    UiModelWriter writer = Ui.ModelWriter(hudModel);
    writer.SetNumber(health, snapshot.Current);
    writer.SetInteger(ammo, snapshot.Ammunition);
}
```

The final API should use existing naming and strong-id conventions. The important contract is compiled property identity and copied ownership.

### 8.2 Dirty propagation

A setter compares against the stored value. An unchanged value does not mark the property dirty. Dirty properties notify only the affected RmlUi model binding. The adapter does not mark the whole document dirty because one scalar changed.

Separate dirtiness domains are measured:

- model dirty;
- style dirty;
- layout dirty;
- text measurement dirty;
- paint dirty.

Do not respond to a blinking cursor or color change by rebuilding document layout.

### 8.3 Actions

Conceptual markup:

```xml
<button data-sencha-action="pause.resume">Resume</button>
<input type="range"
       data-sencha-preview="settings.preview_master_volume"
       data-sencha-commit="settings.commit_master_volume" />
```

Conceptual consumption:

```cpp
for (const UiActionEvent& event : ui.DrainActions())
{
    switch (event.Action)
    {
    case ResumeAction:
        session.Resume();
        break;
    case PreviewVolumeAction:
        audio.PreviewMasterGain(event.Value.AsNumber());
        break;
    case CommitVolumeAction:
        settings.CommitMasterGain(event.Value.AsNumber());
        break;
    }
}
```

Do not build one central engine switch containing every game action. Action resolution is document-local and game-owned. The engine validates identity and payload type, queues values, and exposes them.

### 8.4 ECS bridge

Presenter systems:

- use cached queries or direct known-entity const reads;
- state which registry view they read;
- never keep chunk pointers across structural changes;
- never store component pointers in model storage;
- tolerate missing entities and unloaded zones;
- define fallback presentation state explicitly.

Actions that alter ECS:

- target generational entity ids or game-owned stable identifiers;
- validate target lifetime at consumption;
- use `CommandBuffer` for structural changes;
- do not embed registry pointers in action payloads.

A general reflection-to-UI binding layer is deferred. Reflection can help generate model schemas later, but direct component field binding is rejected because it would couple authored UI to component storage, zone lifetime, change detection, and game module layouts.

---

## 9. Rendering contract

### 9.1 Plain render-domain records

`UiRenderData` contains plain values and strong handles only. Expected records include:

- viewport extent and scale;
- ordered batches;
- geometry spans or retained geometry handles;
- texture reference kind and handle;
- scissor rectangle;
- transform;
- blend state fixed by the UI profile.

No record contains an `Rml::Element*`, `Rml::Context*`, `Registry*`, callback, game object pointer, or Vulkan handle.

### 9.2 Geometry lifetime

RmlUi may compile geometry for reuse. The adapter stores compiled CPU geometry in a generational cache. Packet batches retain cache handles until packet reset. Release from RmlUi drops the document's reference but does not invalidate an in-flight packet.

The first backend may copy referenced geometry into per-frame scratch buffers during draw. This keeps Vulkan allocation out of UI extraction and avoids persistent per-element GPU buffers. Record:

- total vertices and indices copied;
- batch count;
- bytes uploaded;
- scratch overflow or growth;
- time in UI extraction and UI recording.

Move to persistent GPU geometry only if measurements show the copy path is material.

### 9.3 Texture kinds

Use two explicit texture domains:

- external texture assets, referenced by `TextureHandle` and retained through package/document lifetime;
- generated UI textures, referenced by a UI-generated texture handle and uploaded by `UiRenderFeature`.

Do not reinterpret one handle type as the other. Packet texture records carry a tag and the matching strong handle.

### 9.4 Clipping and transforms

Rectangular clipping maps to Vulkan scissor state. Consecutive batches with equal texture, transform class, and scissor may merge when geometry ordering permits.

Transforms remain per batch or are applied during CPU recording. The choice must preserve RmlUi ordering and be benchmarked. No global sort may reorder translucent UI geometry.

### 9.5 Color and alpha

Pin one color space and alpha convention across generated vertices, texture upload, shaders, and swapchain composition. The recommended first contract is linear shader math with premultiplied alpha output and explicitly tagged sRGB external textures.

Add focused render tests for edge alpha, text atlas sampling, overlapping translucent panels, and disabled scissor state. Do not tune until screenshots look acceptable and then leave the convention undocumented.

---

## 10. Execution stages

Each stage lands independently with the full suite green. A stage that cannot remain green is too broad or has crossed an unacknowledged contract.

### Stage 0. Record the architecture and repair ABI coverage

Purpose: make later public and packet changes mechanically safe.

Work:

1. Add this execution document and update the roadmap item.
2. Audit the actual module-facing header closure.
3. Replace the ad hoc fingerprint glob list with one authoritative ABI header manifest.
4. Add the runtime, input, render packet, strong-id, and future UI public records that cross the boundary.
5. Add or extend layout and skew tests.
6. Correct stale ABI documentation to reflect what is already implemented and what remains incomplete.

Tests and gates:

- a token change in a transitive ABI header changes the fingerprint;
- comment-only and whitespace-only changes retain the established semantic-hash behavior;
- an intentionally skewed module is rejected before C++ calls cross the boundary;
- module isolation and existing ABI tests pass.

Stop condition: if the existing module loader cannot validate a changed fingerprint before touching an unsafe vtable, split and finish the ABI prerequisite before UI public API work.

### Stage 1. Consolidate runtime asset operations and preload dependency ordering

Purpose: remove the branch pile that UI packages and fonts would otherwise deepen.

Work:

1. Introduce one fixed table of runtime operations keyed by `AssetType`.
2. Register all existing asset loaders and resident operations into it.
3. Introduce an internal owning type-erased preload lease.
4. Migrate `AssetPreload` to one lease collection.
5. Migrate `AssetPreloader` acquisition, commit, delivery, and release to the table.
6. Replace hardcoded material waves with deterministic dependency records.
7. Preserve typed `AssetSystem` convenience methods and current cache handle semantics.
8. Delete old duplicate switches after migration.

Tests and gates:

- every currently supported asset type retains identical acquire/release behavior;
- cancellation releases every lease exactly once;
- shared in-flight loads retain one committed resource per waiter;
- dependency order is deterministic under reversed async completion order;
- missing dependency, failed dependency, and cycle cases terminate with diagnostics;
- zero-worker and threaded async paths produce equivalent outcomes;
- existing material preload tests remain green without a material-specific coordinator branch.

Stop condition: do not introduce a virtual cache hierarchy or generic untyped public load API merely to remove switches. The type-erased mechanism is internal to shared preload operations; typed consumers stay typed.

### Stage 2. Establish complete input routing

Purpose: give UI and gameplay one honest input owner.

Work:

1. Define plain `InputEvent` records and `InputEventDisposition`.
2. Add SDL translation with full modifiers, text, composition, pointer, controller, focus, and window data.
3. Add `InputRouter` with ordered route slots and lifetime-safe registration.
4. Move `InputFrame` ownership and snapshot folding into the router.
5. Update `FrameDriver` and phase contexts to consume the router-owned snapshot.
6. Reorder the platform pump so consumption occurs before terminal gameplay capture.
7. Migrate ImGui event processing to the overlay route.
8. Migrate CubeDemo relative mouse capture so UI-consumed pointer events cannot enable it.
9. Preserve lifecycle observation regardless of consumption.

Tests and gates:

- ordered events remain ordered;
- modifiers and text composition survive translation;
- consumed events do not affect `InputFrame`;
- unconsumed events preserve current held and edge behavior;
- zero-tick frames retain gameplay edges;
- focus loss clears capture and held state according to the pinned policy;
- destroyed consumers cannot be called;
- ImGui overlay, runtime UI placeholder route, application route, and gameplay sink run in fixed order.

Manual path:

- open debug overlay over a running scene;
- interact with it without moving the camera or firing gameplay controls;
- close it and verify gameplay immediately resumes;
- focus-loss during pointer capture terminates capture.

### Stage 3. Add UI package and font asset types

Purpose: create the shipping content path before any loose-file prototype becomes entrenched.

Work:

1. Add `UiPackage` and `Font` asset types and runtime handles.
2. Add caches with generational identity and explicit retain/release.
3. Add source importers under `SENCHA_ENABLE_COOK`.
4. Add deterministic `.sui` and font cooked formats.
5. Register loaders through the Stage 1 operation table.
6. Extend asset scanning, cooked index mapping, manifests, and hot-reload classification.
7. Add package dependency extraction for textures and fonts.
8. Reject raw filesystem escape and unsupported profile features during cook.

Tests and gates:

- source package cooks deterministically;
- cooked package loads with cook disabled;
- malformed and oversized records fail without partial cache publication;
- dependency records resolve id first and path fallback second;
- package and font handles retain and release correctly;
- hot reload failure leaves the previous cache entry resident;
- no RmlUi or FreeType header leaks through public asset headers.

### Stage 4. Integrate the private RmlUi runtime

Purpose: land retained documents, models, actions, and headless update without Vulkan.

Work:

1. Pin RmlUi and configure only the required core and font features.
2. Add the dependency firewall fitness test.
3. Implement the custom system, package file, font, and text-input interfaces.
4. Implement `UiRuntime` behind a private implementation.
5. Add strong context, document, model, property, and action ids.
6. Load documents only from resident `UiPackage` handles.
7. Bind RmlUi models to Sencha-owned storage through getter/setter adapters.
8. Queue semantic actions without invoking game code.
9. Support headless context creation, update, layout, and CPU paint recording.
10. Add explicit shutdown ordering around RmlUi's non-owning global interface pointers.

Tests and gates:

- repeated initialize/shutdown in one process is either supported and tested or explicitly forbidden with a clear assertion;
- context and document handles reject stale generations;
- unloading a package cannot invalidate a live document reference;
- game-owned input values are copied, never borrowed;
- action source destruction during dispatch does not invalidate queued events;
- headless tests load a package, update a model, trigger a button, and produce a bounded paint list;
- no worker thread calls RmlUi or mutates runtime caches.

### Stage 5. Add model and action public API

Purpose: establish the game-module boundary before a sample invents ad hoc access.

Work:

1. Add `Engine::Ui()` access with explicit headless behavior.
2. Add typed model schema and writer APIs.
3. Add immutable list snapshots only for the first real list consumer.
4. Add action registration or resolution and drain APIs.
5. Add payload type validation.
6. Add module ABI records and tests.
7. Document presenter and action-consumer patterns in the template project.

Tests and gates:

- an out-of-tree module creates a model, writes values, loads a document, and drains actions;
- module unload destroys or invalidates every module-owned registration before code is unmapped;
- a stale model, property, document, or action handle fails locally;
- type mismatch reports authored and registered types;
- no RmlUi type appears in installed public API.

Stop condition: if callbacks owned by the game module would remain stored after module unload, replace them with action queues or explicit registration tokens before proceeding.

### Stage 6. Add immutable render extraction and Vulkan UI phase

Purpose: complete the render-domain boundary.

Work:

1. Add `UiRenderData` to the render packet and ABI manifest.
2. Implement the recording RmlUi render interface and CPU geometry cache.
3. Add packet retention for geometry and texture handles.
4. Add runtime UI and development overlay render phases in final order.
5. Implement `UiRenderFeature` with dynamic vertex/index upload, shaders, descriptors, scissor, transforms, and generated texture publication.
6. Move ImGui to the topmost overlay phase.
7. Integrate swapchain resize, DPI, device teardown, and no-document paths.
8. Add UI timing and batch metrics.

Tests and gates:

- renderer feature sees no RmlUi or ECS types;
- packet reset releases all retained handles;
- document mutation after extraction cannot affect the packet being rendered;
- zero documents and many documents work;
- invalid generated texture handles skip bounded batches with diagnostics rather than dereferencing stale resources;
- UI renders after scene content and before ImGui;
- resize and swapchain recreation preserve document state and rebuild only GPU-dependent resources;
- shipping build with debug UI off renders runtime UI.

Performance gate:

Record CPU update, layout, extraction, upload bytes, batch count, and GPU UI timing for the vertical slice at 1080p and 4K. Do not set a universal budget until this measurement exists. No per-frame source parsing or unbounded allocation is permitted.

### Stage 7. Ship the HUD vertical slice

Purpose: prove the ECS to presentation boundary.

Work:

1. Add a template-game HUD package.
2. Add a concrete presenter system that reads existing gameplay data with const access.
3. Resolve model properties once at startup.
4. Show health and one changing secondary value.
5. Add one conditional class or visibility expression.
6. Add a localization-ready text lookup seam without implementing full localization.

Tests and gates:

- no UI code reads a registry directly;
- missing player or unloaded registry produces declared fallback state;
- unchanged model values do not trigger redundant dirty work;
- HUD operates with zero, one, and many active zone registries according to its explicit source policy;
- shipping configuration renders HUD with ImGui absent.

### Stage 8. Ship pause, settings, modal, and navigation

Purpose: prove real interaction rather than static presentation.

Work:

1. Add button, range control, modal focus scope, and document navigation examples.
2. Add keyboard and pointer focus/capture behavior.
3. Add controller spatial navigation through the input route and RmlUi navigation facilities.
4. Add explicit preview, commit, and cancel settings actions.
5. Restore prior focus when a modal closes.
6. Define pause document ownership and simulation-timescale behavior in application code.
7. Add cursor and relative-mouse transitions.

Tests and gates:

- opening the pause menu prevents gameplay input in the same platform frame;
- closing it restores gameplay input without a stuck held key or pointer button;
- slider capture continues outside its bounds and terminates on release or focus loss;
- Escape closes the top modal before it resumes or exits the game;
- controller navigation is deterministic and trapped inside modal scope;
- settings preview cancels on interruption and commit persists through the owning settings path;
- no UI event callback directly changes ECS structure.

### Stage 9. Add hot reload and state transfer

Purpose: make markup and CSS a practical authoring loop.

Work:

1. Add UI source watching through the existing hot-reload driver.
2. Recook packages through the same importer used at startup.
3. Stage off-thread and commit on the owner thread.
4. Snapshot supported document state by authored id.
5. Rebuild affected documents and transfer compatible state.
6. Keep the last good package on any failure.
7. Add diagnostics to the debug service and console.

Tests and gates:

- stylesheet-only changes update presentation without losing focus or scroll;
- compatible markup changes retain supported state;
- incompatible node replacement falls back predictably;
- deleting the focused node selects the declared fallback;
- failed cook, failed package load, and failed document rebuild retain the old UI;
- repeated reload does not leak package, geometry, texture, document, or action handles.

### Stage 10. Harden, document, and gate

Purpose: turn the vertical slice into engine infrastructure.

Work:

1. Add installed SDK documentation for authoring, models, actions, input, supported RCSS, and diagnostics.
2. Add content examples to the template project.
3. Add UI dependency, ECS isolation, Vulkan isolation, cook isolation, and public-header fitness tests.
4. Add bounded stress fixtures for deep trees, large text, many style rules, many generated elements, and repeated reload.
5. Add a clean-machine packaged smoke test on Linux and Windows when the platform track is available.
6. Update `docs/core-systems-map.md` from verified landed code.

Gate:

The complete vertical slice cooks, packages, runs with no source files, handles keyboard, pointer, controller, and text focus correctly, hot-reloads in development, and passes with debug UI disabled. The renderer never traverses live UI or ECS state, and the public module API exposes no third-party types.

---

## 11. Compatibility, threading, and performance consequences

### ABI

- `Engine::Ui()` and public UI records are module ABI changes.
- `RenderPacket` extension is a public layout change even when existing game code only sees it through a reference.
- Input ownership changes should preserve `InputFrame` behavior, but any public context layout change is ABI-significant.
- Stage 0 must make every such change automatically visible to the fingerprint.

### Persisted and cooked formats

- `.sui` and the cooked font format begin at version 1 with explicit magic and bounds.
- Adding `AssetType` values is additive but affects string serialization, scanners, cooked indices, manifests, and tests.
- Hot reload never silently substitutes a default document after a failed package migration.

### Threading

- SDL translation, input dispatch, RmlUi update, model mutation, action queue mutation, document lifetime, cache commit, and GPU publication remain owner-thread work.
- File reads, source cook, package validation that does not touch RmlUi global state, and plain CPU staging may use `AsyncTaskQueue`.
- No RmlUi call runs on `JobSystem` or task threads unless upstream documentation and a focused design explicitly prove that call independent of global/context state. The default is owner thread only.
- No third worker lane, raw thread, lock-first ownership repair, or background UI animation loop is introduced.

### Determinism

- UI is presentation behavior and does not participate in fixed simulation determinism.
- Input consumption is ordered and deterministic for a given event stream.
- Cooked package output, action resolution, selector/profile validation output, dependency ordering, and diagnostic ordering are deterministic.
- UI actions affecting deterministic simulation enter through the same game command/intention path as other player input at a declared boundary.

### Performance

Expected complexity:

- input dispatch is O(events times active input layers), with a fixed small layer count;
- model writes are O(writes), with O(1) handle lookup;
- style/layout work is proportional to dirty documents and affected subtrees as provided by RmlUi;
- paint extraction is proportional to emitted paint operations and visible geometry;
- draw submission is proportional to ordered batches and uploaded geometry;
- asset dependency resolution is O(nodes plus edges) for each preload graph.

Forbidden hot-path work:

- source file reads;
- RML or RCSS source discovery;
- action-name string lookup per frame;
- ECS reflection traversal per property per frame;
- arbitrary asset loads from `UiRenderFeature::OnDraw`;
- full-document rebuild for scalar model changes;
- GPU object creation on task threads;
- renderer traversal of RmlUi or ECS state.

---

## 12. Diagnostics

Expose bounded runtime diagnostics through the existing debug and console systems:

- loaded contexts and documents;
- package and font handle counts;
- focused, hovered, and captured element ids;
- active modal scope;
- model writes and dirty property count;
- layout and paint extraction time;
- element count and maximum depth;
- geometry vertices, indices, batches, and upload bytes;
- generated texture count and bytes;
- action queue depth and dropped-action count;
- last cook/load/rebuild error with source location;
- hot-reload generation and state-transfer summary.

Diagnostics must not require ImGui to exist. ImGui may display them, but the owning counters and records live in renderer-agnostic UI or debug state.

Errors are local and actionable:

- unknown action names report package, document, element id, and authored action;
- model mismatch reports model, property, expected type, and supplied type;
- unsupported RCSS reports property and required renderer feature;
- stale handles report type, index, and generation in development builds;
- dependency failures show the dependency chain;
- state-transfer failure names the element id and incompatibility.

---

## 13. Explicit non-goals

The first execution does not include:

- HTML compatibility;
- web page loading;
- JavaScript;
- RmlUi Lua bindings;
- arbitrary DOM access from game modules;
- a general reflection-to-ECS binding system;
- world-space UI surfaces;
- editor chrome migration;
- browser accessibility APIs;
- full international shaping and fallback beyond the initial font contract;
- advanced filters, backdrop filters, masks, blurred shadows, or arbitrary UI shaders;
- a visual drag-and-drop UI editor;
- network replication of UI state;
- CSS extensions for gameplay behavior;
- a second UI framework beside RmlUi and ImGui.

Each has a concrete revisit trigger. None is represented by dormant interfaces, enum members, package fields, or half-wired code.

---

## 14. Stop conditions

Stop and return to owner review instead of improvising when:

- RmlUi requires a public third-party type to provide a needed feature;
- an implementation wants a raw `Registry&`, component pointer, or world pointer inside `UiRuntime`;
- a model binding would borrow game memory beyond the current call;
- an input consumer cannot receive the complete event without changing the public module contract;
- the clean input route requires preserving both old and new owners of `InputFrame`;
- a UI asset type would require another `AssetPreloader` switch or hardcoded wave;
- dependency graph work materially changes every existing synchronous load contract;
- the renderer would need to call live RmlUi during `OnDraw`;
- a packet cannot safely retain compiled geometry across double-buffered lifetime;
- an unsupported RCSS feature has no reliable cook-time detection;
- font loading would bypass the asset pipeline;
- hot reload cannot retain the last good document atomically;
- a public record is not covered by ABI fingerprint and layout tests;
- a stage needs a lock, raw thread, third task lane, or worker-thread GPU operation;
- the first shipping use requires world-space UI, advanced rendering effects, or editor migration.

A stop condition is not permission to add an adapter around the bad contract. Identify the contract, propose the smallest correction, and update this execution document before continuing.

---

## 15. Definition of done

Runtime UI is complete for the first shipping cut only when:

- the invariant has the owners named in this document;
- input has one ordered route and one gameplay snapshot owner;
- UI documents are retained outside gameplay registries;
- game state enters through copied, typed, handle-resolved model values;
- actions leave through bounded semantic events;
- the asset system has no UI-specific loader or preload side path;
- RmlUi is private and guarded by a fitness test;
- source markup and styles cook into deterministic packages;
- fonts and textures use the normal asset and cache lifetimes;
- the renderer consumes immutable packet data only;
- runtime UI and debug overlay have explicit render order;
- HUD, pause, settings preview/commit, modal focus, controller navigation, and hot reload are exercised;
- focus loss, resize, minimize, document unload, package reload, module unload, and shutdown terminate interaction and release resources;
- headless tests cover model, action, layout, package, and paint contracts;
- shipping configuration renders UI with ImGui compiled out;
- focused tests, canonical build, full serial ctest suite, ABI checks, dependency fitness tests, `git diff --check`, and the real interaction path pass;
- unrun platform or packaging verification is reported honestly.

---

## 16. Primary implementation references

Use upstream documentation as behavior evidence, not as permission to import upstream architecture wholesale:

- RmlUi repository and feature overview: <https://github.com/mikke89/RmlUi>
- Custom interface ownership: <https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/interfaces.html>
- Render interface feature requirements: <https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/interfaces/render.html>
- File interface: <https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/interfaces/file.html>
- Font engine interface: <https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/interfaces/font_engine.html>
- Text input handler: <https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/interfaces/text_input_handler.html>
- Data model pointer lifetime and function binding: <https://mikke89.github.io/RmlUiDoc/pages/data_bindings/model.html>
- Data-binding limitations: <https://mikke89.github.io/RmlUiDoc/pages/data_bindings.html>

Repository references that remain binding:

- `CLAUDE.md`
- `docs/core-systems-map.md`
- `docs/assets/pipeline.md`
- `docs/architecture/hardening-and-consolidation.md`
- `docs/plans/sencha-level-editor/09-module-abi-hardening.md`
- `engine/include/runtime/FrameDriver.h`
- `engine/src/app/EngineFramePhases.cpp`
- `engine/include/input/InputFrame.h`
- `engine/include/runtime/RenderPacket.h`
- `engine/include/graphics/vulkan/Renderer.h`
- `engine/include/core/assets/AssetLoader.h`
- `engine/include/core/assets/AssetSystem.h`
- `engine/src/core/assets/AssetPreloader.cpp`
