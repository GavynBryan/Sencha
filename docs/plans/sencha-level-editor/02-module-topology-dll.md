# Pillar 2 — Module Topology: engine.so · game DLL · app host · editor module pickup

**Status**: Working plan (2026-06-14). Depends on Pillar 1 (01-) landing first.
**Owns Stages S2 (host loads module) and S3 (editor picks up module).**

> Target state, from 00-overview §2: a **single game module artifact** is *loaded at
> runtime* by both the `app` host and the `sencha_editor`, never statically linked per tool.
> This is what makes Sencha shippable to multiple game teams who use our prebuilt tools.

---

## 1. Current state (what we are changing)

- `engine` is a **static** library (`add_library(sencha_engine STATIC …)`,
  [engine/CMakeLists.txt:90](../../engine/CMakeLists.txt)). Its globals (the serializer
  registry, asset front door) get a copy per linking exe — fine today because each exe is
  self-contained, fatal the moment two modules must share one registry.
- `app/main.cpp` is a stub (`return 0;`). There is **no host** and **no game library**.
- Each demo (`CubeDemo`, …) is a self-contained exe linking the engine directly and
  hard-coding its own components/systems.
- `sencha_editor` links `sencha::engine` and hard-knows `BrushComponent` only.

We turn this into: **engine.so** (shared, single instance of its globals) · **game.so**
(loadable, per title) · **app** (host that loads engine + a game) · **editor** (loads engine
+ game modules at runtime).

---

## 2. The module ABI — `IGameModule`

A game module exposes **exactly one** C-linkage entry point. Everything else crosses the
boundary as C++ vtables created *inside* the module (the pattern that already works for
`IComponentSerializer`, per 01-§1).

```cpp
// engine/include/app/GameModule.h  (new, engine-owned)
#pragma once
#include <cstdint>

struct GameModuleContext;   // forward; engine-owned services handed to the module

// The one stable contract a game module fulfils. Pure interface; the module returns a
// concrete instance from its single exported factory. No engine-global state lives here —
// the module is handed the engine's services via the context and calls INTO them.
struct IGameModule
{
    // Identity/versioning so the host can refuse an incompatible build.
    virtual std::string_view Name() const = 0;
    virtual std::uint32_t    AbiVersion() const = 0;   // == SENCHA_GAME_ABI_VERSION at build

    // Called once at load. Register components (serializers + storage), prefabs, and
    // resource/system factories into the engine-owned registries reachable via ctx.
    // MUST NOT create entities or touch a World's instance data — registration only.
    virtual void Register(GameModuleContext& ctx) = 0;

    // Called once at unload (editor module swap / host shutdown). Symmetric teardown.
    virtual void Unregister(GameModuleContext& ctx) = 0;

    virtual ~IGameModule() = default;
};

// The ONLY exported symbol. C linkage so the loader can resolve it by a fixed name and so
// no C++ name mangling crosses the boundary.
extern "C" SENCHA_GAME_EXPORT IGameModule* SenchaCreateGameModule();
```

```cpp
// SENCHA_GAME_ABI_VERSION bumps whenever IGameModule / GameModuleContext /
// the registration surface changes shape. The host compares and refuses on mismatch,
// turning an ABI skew into a clean error instead of a crash.
#define SENCHA_GAME_ABI_VERSION 1
```

`GameModuleContext` is the **inversion-of-control seam** — it hands the module references to
the engine's *single* registries so the module never hosts its own:

```cpp
// engine/include/app/GameModule.h
struct GameModuleContext
{
    // The engine-owned component-serializer registry (replaces today's free-function
    // global RegisterComponentSerializer so ownership is explicit and single-instance).
    ComponentSerializerRegistry& Serializers;

    // Prefab registry (component templates the editor can instantiate). 02-§6.
    PrefabRegistry& Prefabs;

    // System/resource factory registry the runtime host uses to build a game's systems
    // for a zone. The editor ignores these (it does not run gameplay) except under PIE.
    GameSystemRegistry& Systems;

    // Engine version info, asset roots, logging sink — read-only services.
    const EngineHostInfo& Host;
};
```

### 2.1 Why the engine must be shared, and globals must be single-instance

The serializer registry today is a **free-function global**
(`RegisterComponentSerializer` / `GetComponentSerializerEntries`,
[SceneSerializer.h](../../engine/include/world/serialization/SceneSerializer.h)). With a
static engine linked into both host and module, each gets its **own copy** of that global —
the module registers into its copy, the host reads its copy, they never meet. Two fixes,
applied together:

1. **engine becomes a shared library**, so there is one copy of its globals process-wide.
2. **Engine globals that the module must reach become explicitly-owned objects passed via
   `GameModuleContext`**, not file-scope statics. The free-function registry is refactored
   into a `ComponentSerializerRegistry` object owned by the host/engine and handed to the
   module. This removes the "which copy?" ambiguity entirely and is the right shape even
   without DLLs (testable, no hidden global state).

> This is the concrete cash-out of 00-overview's rule *"game/editor never host engine-global
> state — they call into the engine's single instance."*

### 2.2 Export/visibility macros

```cpp
// engine/include/app/ModuleExport.h
#if defined(_WIN32)
  #define SENCHA_GAME_EXPORT __declspec(dllexport)
  #define SENCHA_GAME_IMPORT __declspec(dllimport)
#else
  #define SENCHA_GAME_EXPORT __attribute__((visibility("default")))
  #define SENCHA_GAME_IMPORT
#endif
```

Engine and game both build with `-fvisibility=hidden` (the hostile case the S0 spike
validated); only the one factory symbol and the engine's intentionally-exported ABI surface
are `default`. This keeps the boundary minimal and is exactly why 01-'s identity scheme must
not rely on RTTI merging.

---

## 3. The module loader

```cpp
// engine/include/app/GameModuleLoader.h  (new)
class GameModuleLoader
{
public:
    // dlopen/LoadLibrary the artifact, resolve SenchaCreateGameModule, ABI-check, call
    // Register(ctx). Returns a handle; keeps the library mapped until Unload.
    LoadedModule Load(const std::filesystem::path& artifact, GameModuleContext& ctx);
    void         Unload(LoadedModule& module, GameModuleContext& ctx);  // Unregister + dlclose
};
```

Contract:
- **ABI check first.** `module->AbiVersion() != SENCHA_GAME_ABI_VERSION` → reject with a
  clear message; never call `Register`.
- **Ownership/allocator discipline.** Anything the module allocates and hands to the engine
  (e.g. `unique_ptr<IComponentSerializer>`) must be *owned and freed consistently*. Rule:
  objects the module creates are owned by the module and freed in `Unregister`, OR the
  engine takes ownership through an interface whose deleter virtual-dispatches back into the
  module. We pick **module-owns, Unregister-frees** for serializers/prefabs (symmetry, no
  cross-allocator `delete`). The engine's registries hold *non-owning* references for the
  module's lifetime. Document this as the binding ABI rule.
- **Keep the library mapped** for the whole time its types are reachable. Unload only when
  no `World` holds entities of its components (editor enforces on module swap; host unloads
  at shutdown).

---

## 4. `app` becomes the runtime host (the "marriage" layer)

`app/main.cpp` grows into the thin host that game devs never edit:

```cpp
// app/main.cpp (sketch)
int main(int argc, char** argv) {
    HostConfig cfg = ParseHostConfig(argc, argv);     // which game module, which startup zone
    Application application;
    GameHost host{ cfg };                              // owns engine services + module loader
    return application.Run(host);                      // host loads game.so, runs its zone(s)
}
```

`GameHost` owns the engine-side singletons (serializer registry, asset system, etc.), loads
the configured `game` module via `GameModuleLoader`, and drives the normal runtime loop. The
**game module supplies** components, prefabs, and the systems for its zones; the host supplies
the engine. Neither the game team nor we edit `app` per title — the title is selected by
config/CLI.

> `LevelDemo` (06-) is the first concrete game module + a host invocation. It replaces the
> self-contained `CubeDemo` exe shape. The demo proves the host↔module path; it is not the
> architecture's customer.

---

## 5. The editor loads game modules

The editor's defining new ability: **open a project, load that project's game module(s), and
author with their components/prefabs** — with no editor rebuild.

### 5.1 Editor project → module

`EditorProject` (today a stub per `docs/SenchaEditor.md`) gains the path(s) to the game
module artifact(s) for the project. On project open, the editor:

1. Builds a `GameModuleContext` over its **own** engine-owned registries (the editor hosts
   the engine the same way `app` does — it is just a different host that draws viewports
   instead of running gameplay).
2. `GameModuleLoader.Load(projectModule, ctx)` → the module's `Register` populates the
   editor's serializer + prefab registries.
3. The editor's `LevelDocument` registry now knows every game component for serialization,
   and the prefab registry feeds the "create" tools and asset/prefab palette.

### 5.2 The editor does NOT run gameplay (except under PIE)

The editor loads the module for its **registration surface** (components, prefabs, schemas),
not to tick game systems. Authoring a `PlayerController` component means editing its
serialized fields — not running it. The module's `Systems` factories are used only by the
**PIE** path (05-) and by the runtime host, never by plain authoring. This keeps the editor
stable: buggy game *logic* cannot crash the editor during authoring, only buggy game
*registration* could (and that fails loudly at load).

### 5.3 Generic, schema-driven inspector & hierarchy (the actual "pickup")

This is where the editor stops hard-coding components. Today:

- `InspectorPanel::OnDraw` hard-lists Transform/Brush/Camera
  ([editor/ui/InspectorPanel.cpp:116-118](../../editor/ui/InspectorPanel.cpp)). Field drawing
  is already generic (`DrawSchemaFields`, [line 61](../../editor/ui/InspectorPanel.cpp)).
- `LevelScene` exposes typed `TryGetBrush/TryGetCamera` accessors; commands are typed
  (`EditComponentCommand<T>`).

The rewrite makes enumeration **registry-driven**:

```cpp
// Inspector: for the selected entity, iterate every registered component serializer,
// ask which are present, and draw each via its schema. No component named in editor code.
for (const IComponentSerializer& s : ctx.Serializers.Entries()) {
    if (!s.HasComponent(entity, registry)) continue;
    DrawComponentSectionDynamic(s, entity, registry);   // schema-driven fields + command
}
```

This requires a **type-erased schema-draw + edit-command path**, because the editor can no
longer name `T`. Two sub-pieces:

- **Type-erased field drawing.** `DrawSchemaFields` is templated on `T` today. Add a
  type-erased entry: the serializer (which *does* know `T`) exposes a
  `DrawInspector(void* component, ImGuiEditScratch&)` that internally calls the templated
  `DrawSchemaFields<T>`. The vtable crosses the boundary; `T` stays in the module. (Same
  trick as serialization.)
- **Type-erased undoable edit.** `EditComponentCommand<T>` needs a non-templated sibling
  that snapshots/restores a component's raw bytes (components are trivially copyable —
  01-/World guarantees memcpy-relocatable). A `RawComponentEditCommand` stores
  `ComponentId` + before/after byte buffers and applies via `World`'s raw component API
  ([`AddComponentRaw`/raw accessors, World.h:554+](../../engine/include/ecs/World.h)).
  This makes editing **any** component — engine or game — undoable without naming it.

`SceneHierarchyPanel` gets the same treatment: it already lists entities by id; add a
component-summary column driven by the serializer registry, no hard-coded type list.

### 5.4 Creating game entities/prefabs in the editor

Placing a game entity (e.g. a player start) is **prefab instantiation** (§6) or a generic
"add component" action driven by the registry: the editor offers every registered component
the project's module declared as `Addable` (a flag in its serializer/prefab metadata), and
"add" issues a `RawComponentAddCommand`. The player controller is shipped as a **prefab**
(06-) so designers place it as one named thing, not by assembling components.

---

## 6. Prefabs (component templates) — minimal, this branch

Full prefab *assets* are forecast (pipeline.md Decision O); this branch ships the **minimum
that makes the player controller placeable and the door open**:

```cpp
// engine/include/world/prefab/Prefab.h  (new, engine-owned)
// A named template: an ordered list of (ComponentTypeId, default-value bytes). Instantiation
// creates an entity and adds each component from the template; per-instance overrides are
// applied on top (transform always; others later).
struct PrefabComponentInit { ComponentTypeId Type; std::vector<std::byte> DefaultBytes; };
struct Prefab { std::string Name; std::vector<PrefabComponentInit> Components; };

class PrefabRegistry {
public:
    void Register(Prefab prefab);              // modules register theirs in IGameModule::Register
    const Prefab* Find(std::string_view name) const;
    std::span<const Prefab> All() const;
    EntityId Instantiate(std::string_view name, World& world, const Transform3f& at) const;
};
```

- A game module registers its prefabs (e.g. `"acme.player_start"`) in `Register(ctx)`.
- The editor lists `PrefabRegistry::All()` in a palette; placing one issues an undoable
  `InstantiatePrefabCommand` (records the created entity for undo, like
  `CreateBrushCommand`).
- **Serialization**: an instantiated prefab serializes as **its component instances** in the
  level JSON (no prefab-asset indirection yet) — so a cooked level needs no prefab system at
  runtime. When prefab *assets* land later, instances gain an `asset://` ref + overrides; the
  authored format extends, the cooked format is unaffected (it is already plain components).
  This is the same authored-vs-cooked discipline as brushes.

> Decision recorded: **component-on-entity now, prefab-asset later.** The player controller
> is a game component placed via a prefab template; the level stores the component instance.
> Trigger to upgrade to prefab-assets: first need for a shared, overridable, instanced
> template across many placements.

---

## 7. CMake / build topology changes

- `engine`: `STATIC` → `SHARED`. Audit exported symbols; add `ModuleExport.h` macros to the
  intentionally-public ABI (the module-facing headers). Hidden visibility default.
- **new** `game/` top-level (sibling of `engine/`, `editor/`, `app/`): builds the game
  module as `MODULE`/`SHARED`. For the engine's own dogfood title this is `LevelDemo`'s game
  code; third parties supply their own `game` against the installed engine ABI headers.
- `app`: links `engine` (shared), gains the host + loader sources, takes a module path via
  config. No game code.
- `editor`: links `engine` (shared), gains the module-loader integration; **drops any
  game-specific compile dependency**. `BrushComponent` and editor-only types remain in
  `editor/`.
- An install/export target so a third party gets `engine.so` + public ABI headers + the
  prebuilt `editor` and `app` — the "SDK drop" that makes "build only your game" real.
  (Build the *seam*; a full installer is out of scope — trigger: first external consumer.)

---

## 8. Stages & gates

**S2 — host loads a module:**
- engine shared; `IGameModule`/`GameModuleContext`/loader landed; serializer registry
  refactored from free-function global to owned object.
- `app` host loads a trivial `game.so` defining one game component + one system; runs a
  zone JSON that uses the component; renders.
- The S0 spike's positive assertions become a **kept** integration test here, on the real
  loader, hidden visibility.
- *Gate:* `app --game <path> --zone <path>` loads the module, the game component drives a
  visible result, **zero static game linkage** (grep the link lines), ABI mismatch is a clean
  refusal.

**S3 — editor picks up the module:**
- Editor loads a project's module; serializer + prefab registries populated.
- Inspector & hierarchy are registry-driven (type-erased draw + `RawComponentEditCommand`);
  no component named in editor code except editor-only `BrushComponent`.
- Editor can add a game component / instantiate a game prefab, edit it, save, re-load.
- *Gate:* a component defined **only in `game/`** appears in the editor inspector, is edited
  undoably, saved, and re-loaded identically — with no edits to `editor/` to support that
  specific component.

---

## 8b. Progress log

- 2026-06-14 — **S2 architectural seam landed; full suite green (871 tests, was 867).**
  - `engine` is now a **SHARED** library ([engine/CMakeLists.txt](../../engine/CMakeLists.txt));
    `CMAKE_POSITION_INDEPENDENT_CODE` is on globally so the static third-party deps link
    into it. One process-wide copy of engine globals — the precondition for module loading.
  - The free-function serializer global was refactored into an owned
    **`ComponentSerializerRegistry`** object
    ([ComponentSerializerRegistry.h](../../engine/include/world/serialization/ComponentSerializerRegistry.h));
    the legacy `RegisterComponentSerializer`/`ClearComponentSerializers`/
    `GetComponentSerializerEntries` free functions are now a thin shim over a process-default
    instance, so existing callers are untouched while the object can be handed to a module.
  - ABI surface landed: [ModuleExport.h](../../engine/include/app/ModuleExport.h)
    (`SENCHA_GAME_EXPORT`, `SENCHA_GAME_ABI_VERSION`) and
    [GameModule.h](../../engine/include/app/GameModule.h) (`IGameModule`, `GameModuleContext`).
  - [GameModuleLoader](../../engine/include/app/GameModuleLoader.h) implements
    open → resolve `SenchaCreateGameModule` → **ABI-check (before Register)** → `Register(ctx)`,
    and `Unload` = `Unregister` + unmap. POSIX (`dlopen`) + Windows (`LoadLibrary`) paths.
  - `World` gained runtime, type-erased identity lookup: `GetComponentIdByType(ComponentTypeId)`
    / `IsRegistered(ComponentTypeId)` — needed by loaded modules and the S3 editor inspector.
  - **Kept integration test** ([GameModuleLoaderTests.cpp](../../test/runtime/GameModuleLoaderTests.cpp))
    on the REAL loader + a real loadable `test_game_module.so` (MODULE, hidden visibility,
    only the factory exported): loads the module, finds its game component the host never
    names by the stable `ComponentTypeId`, registers storage type-erased, round-trips the
    component through the serializer vtable seam (authored JSON → Load → Save), and verifies
    Unload retracts exactly its serializer. Plus loader-contract refusals (missing artifact,
    no factory). This is the S0 spike's assertions, now on the production loader.
  - **§3 ownership note (interim):** the registry currently *owns* the module's serializer
    `unique_ptr`; the module retracts it in `Unregister` (via `ComponentSerializerRegistry::Remove`)
    while still mapped, so no use-after-unmap. This works on Linux (shared libstdc++, one
    allocator). The strict "module-owns / engine holds non-owning ref" rule matters for
    **Windows separate-CRT and hot-reload** — convert then (trigger).
  - **Remaining for S2:** wire `app/main.cpp` into the `GameHost` + CLI (`--game`, `--zone`)
    and the `game/` top-level target. Deferred here because its gate ("game component drives
    a *visible* result") needs GPU rendering to demonstrate, which the current environment
    cannot verify; the load/register/serialize chain it depends on is already proven by the
    kept test above.

## 8c. S3 progress log

- 2026-06-14 — **Type-erased editing keystone landed; 874 tests green.**
  - `World` gained type-erased component access: `GetComponentRaw(EntityId, ComponentId)`
    (const + mutable) and `HasComponent(EntityId, ComponentId)` — the path the
    registry-driven inspector and the raw edit command use without naming `T`.
  - **`RawComponentEditCommand`** ([editor/level/LevelCommands.h](../../editor/level/LevelCommands.h)):
    the non-templated sibling of `EditComponentCommand<T>`; snapshots before/after raw
    bytes by `ComponentId` and applies via `World::GetComponentRaw` (components are
    memcpy-relocatable). Makes ANY component — engine or game-module — undoable without
    the editor naming it. Covered by `test/core/TypeErasedComponentEditTests.cpp`
    (round-trip, undo/redo, absent-component null).
  - **Field-draw decision made + built (type-erased schema descriptor).** Rather than put
    ImGui in the engine/modules, the engine exposes a flattened, type-erased field
    descriptor and the editor owns all drawing:
    [RuntimeSchema.h](../../engine/include/core/metadata/RuntimeSchema.h) flattens a
    component's compile-time `TypeSchema` (recursing through Vec/Quat/Transform) into
    leaf scalars — `{dotted name, byte offset, size, FieldScalar}` — via `RuntimeFieldsOf<T>()`.
    `IComponentSerializer::RuntimeFields()` returns these, so the editor draws/edits ANY
    component by reading/writing scalars at offsets in `World::GetComponentRaw` bytes, with
    no ImGui reaching the engine or game modules. Covered by
    [RuntimeSchemaTests.cpp](../../test/core/RuntimeSchemaTests.cpp) (kinds/sizes, offset
    round-trips, nested dotted paths, enum→underlying). 878 tests green.
  - **Remaining for S3 (UI-coupled, needs a GUI/GPU session to verify the gate):**
    1. Editor module pickup: an `EditorProject` carrying the game-module path, loaded via
       `GameModuleLoader` into the editor's registries on project open. (The load→register→
       serialize chain is already proven headlessly by the S2 loader test; this is wiring.)
    2. Registry-driven `InspectorPanel`: iterate `ComponentSerializerRegistry::Entries()`,
       for each present component draw its `RuntimeFields()` as widgets over the raw bytes,
       and commit through `RawComponentEditCommand`. All the non-UI pieces now exist.
    3. `SceneHierarchyPanel` component-summary column from the registry.

## 9. Risks & mitigations

- **Engine-as-shared-lib surface.** Going shared exposes which symbols are really public.
  Mitigation: explicit export macros on the ABI headers; everything else hidden; CI link of
  a minimal external consumer to catch accidental private-symbol reliance.
- **Allocator/ownership across the boundary.** Pinned by the "module-owns, Unregister-frees"
  rule (§3); no engine-side `delete` of module-allocated objects.
- **ABI drift between editor/host and module.** `SENCHA_GAME_ABI_VERSION` gate; modules
  built against mismatched headers are refused, not crashed.
- **Editor stability vs. buggy game code.** Editor loads modules for registration, not to
  tick game systems (except PIE, which is opt-in and sandboxed). Registration failures are
  loud and isolated.
- **Windows/Linux loader differences.** Loader is abstracted (`dlopen` vs `LoadLibrary`);
  validated on both before S2 gate is called.
