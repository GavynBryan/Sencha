# Sencha Level Editor & Game-Module Architecture — Master Plan

**Status**: Working plan (2026-06-14). Supersedes `docs/plans/editor-brush-geometry-cook.md`,
which scoped only the brush→mesh cook. This suite scopes the full branch.

**Audience**: Whoever implements this (human or agent), and reviewers. Written as an
execution spec: opinionated, concrete, and detailed enough that two people working from
it independently would build the same thing.

---

## 0. Read this first: framing

Sencha is a **commercial, multi-game C++ engine**. It will ship multiple titles across
multiple genres and production tiers. The demos (`CubeDemo`, `AudioTest`, …) are **proof
that a feature works — not the development target.** No decision in this suite is allowed
to optimize for a demo at the expense of the shipping shape. When the two conflict, the
shipping shape wins, every time, even if it costs more now.

Three non-negotiable consequences of that framing, decided with the product owner
(2026-06-14):

1. **Game code ships as a runtime-loaded module (a DLL/`.so`), not a static lib.**
   A third-party game team builds *only their game module* and runs Sencha's *prebuilt*
   tools (the editor, the runtime host). That is only possible if the game is loaded at
   runtime. Static linking would force every team to produce their own editor binary —
   that is shipping a build system, not an engine. See `02-module-topology-dll.md`.

2. **The reflection / component-identity system is rewritten to be boundary-safe.**
   Today component identity is `typeid(T)` keyed into a per-`World` map
   ([engine/include/ecs/World.h:98](../../engine/include/ecs/World.h)). Across a module
   boundary that is fragile (depends on RTTI symbol merging, visibility flags, and STL
   implementation). We replace `typeid`-as-key with an **explicit stable component
   identity** baked identically into every module. This is a ground-up change to the
   ECS type-lookup path and we are doing it deliberately, not patching around it. See
   `01-reflection-and-type-identity.md`.

3. **No "good enough for now."** Every seam is built in its final shape. We may *defer*
   building a feature, but we do not build a feature in a throwaway shape we know we will
   rewrite. Deferral is recorded with an explicit trigger (the pattern
   `docs/assets/pipeline.md` already uses).

---

## 1. What "done" means for this branch

A designer can:

1. Open the **Sencha Editor**, a dedicated Hammer/Source-2-style level-design window.
2. Author a level out of **brushes** — first-class, editable polygonal solids with
   crude mesh-editing verbs (extrude, face delete, clip; carve is a stretch) and
   **per-face texturing with UVs that re-project automatically on resize**.
3. Place a **player start** and other game entities, including a **player controller**
   that is a *game-defined* prefab/component — authored in the game module, **picked up
   automatically by the editor** (inspector, hierarchy, serialization) with zero editor
   code changes.
4. **Save** the level (authored JSON: transforms + brushes + game components).
5. **Cook** the level: brushes compile into a real `StaticMesh` (one mesh per level, one
   section per material), referenced by a cooked scene. No brush concept survives into
   the runtime. No orphaned/garbage assets are left behind, ever.
6. **Play** it — both in-editor (PIE, in-memory, zero disk I/O) and by loading the cooked
   level in **`LevelDemo`** (the renamed `CubeDemo`: a *minimal game module* that links
   the engine and the game library and proves the whole chain).

And a *programmer* can:

7. Define a new component or prefab **in the game module only**, never touching
   `engine/` or `app/`, and have it serialize, inspect, and cook correctly in both the
   runtime and the editor.

The branch is done when all seven hold, each behind the phase gate that owns it.

---

## 2. Architecture target (the end state we build toward)

```
┌───────────────────────────────────────────────────────────────────────────┐
│ engine  (sencha_engine, shared library)                                     │
│   ECS (World, archetypes), reflection (stable component identity),          │
│   serialization, assets + cook, render, runtime/zone, app framework.        │
│   Owns the ONE component-type registry. Knows nothing about any game.       │
└───────────────────────────────────────────────────────────────────────────┘
        ▲                         ▲                              ▲
        │ links                   │ links                        │ links
        │                         │                              │
┌───────────────┐   ┌──────────────────────────┐   ┌──────────────────────────┐
│ game (DLL)    │   │ editor (sencha_editor exe)│   │ app (runtime host exe)   │
│  PlayerCtrl   │   │  loads game module(s) at  │   │  loads a game module +   │
│  components,  │   │  runtime, picks up their  │   │  marries engine+game,    │
│  prefabs,     │   │  components/prefabs via   │   │  runs the cooked game.    │
│  systems.     │   │  the registration ABI.    │   │  Game devs never edit it. │
│  RegisterGame │   └──────────────────────────┘   └──────────────────────────┘
│  Module() ABI │                ▲                              ▲
└───────────────┘────────────────┘──────────────────────────────┘
        the SAME game module, loaded by both editor and runtime host
```

Key invariants of the target:

- **The engine owns the single component-type registry and the single asset front door.**
  Game and editor *call into* it; they never host their own copy of engine-global state.
- **A game module is loaded, not linked.** `editor` and `app` both `dlopen`/`LoadLibrary`
  the same `game` artifact and call its one exported registration entry point.
- **`engine` becomes a shared library** (it is a static lib today —
  [engine/CMakeLists.txt:90](../../engine/CMakeLists.txt)). It must be shared so a single
  copy of its globals/registries is visible to host and game module alike.
- **The editor links the engine and loads game modules**; it has *no* compile-time
  dependency on any specific game. `BrushComponent` and editor-only types stay in the
  editor; everything game-specific arrives via the loaded module.

---

## 3. The four pillars and how they depend on each other

```
        ┌─────────────────────────────────────────────┐
        │ PILLAR 1: Reflection & stable type identity  │  (01-)
        │  typeid-as-key  →  explicit ComponentTypeId  │
        └─────────────────────────────────────────────┘
                 │ gates everything that crosses a module boundary
                 ▼
        ┌─────────────────────────────────────────────┐
        │ PILLAR 2: Module topology (engine.so, game   │  (02-)
        │  DLL, app host, editor module pickup)        │
        └─────────────────────────────────────────────┘
             │                              │
             ▼                              ▼
   ┌───────────────────────┐    ┌──────────────────────────────┐
   │ PILLAR 3: Brush model  │    │ (player prefab, LevelDemo,    │
   │  editable poly mesh    │    │  playable milestone) 06-      │
   │  03-  →  texturing 04- │    └──────────────────────────────┘
   └───────────────────────┘
             │
             ▼
   ┌───────────────────────┐
   │ PILLAR 4: Level cook   │  (05-)
   │  brushes → static mesh │
   │  + PIE + prune         │
   └───────────────────────┘
```

- **Pillar 1 (reflection)** is foundational. Until component identity is module-safe,
  nothing game-defined can be trusted across the editor/runtime/game boundary. **It lands
  first.** It is also the single highest-risk change, so it is gated by a standalone spike
  before any dependent work starts.
- **Pillar 2 (topology)** turns `engine` shared, extracts `game`, makes `app` the host,
  and teaches the editor to load modules. Depends on Pillar 1's identity guarantees for
  the registration ABI to mean anything.
- **Pillar 3 (brush model + texturing)** replaces the box `BrushComponent` with an
  editable polygon-mesh brush and a per-face projection-UV texturing model. Largely
  independent of Pillars 1–2 (it is editor-internal data) and can proceed in parallel
  once the team has capacity — but its *cook* (Pillar 4) wants the final brush + UV shape.
- **Pillar 4 (cook)** compiles brushes to a cooked static mesh, runs in-editor (PIE) and
  offline, and guarantees no asset garbage. Downstream of 3; consumes 1–2's manifest/asset
  identity work.

Document map:

| Doc | Pillar | What it specifies |
|-----|--------|-------------------|
| `01-reflection-and-type-identity.md` | 1 | Stable `ComponentTypeId`, registry rewrite, the boundary spike. |
| `02-module-topology-dll.md` | 2 | engine.so, game DLL, `IGameModule` ABI, app host, editor module loader, generic inspector/hierarchy. |
| `03-brush-representation.md` | 3 | Editable polygon-mesh brush; clip/extrude/delete; selection & handles; serialization. |
| `04-brush-texturing-uv.md` | 3 | Per-face material slots, projection UVs (texture lock), inspector & viewport UV tools. |
| `05-level-cook.md` | 4 | Bake to static mesh (sections per material), `AssetSourceKind::Generated`, cooked-cache participation, prune, PIE, glTF/asset bake-out. |
| `06-leveldemo-player-playable.md` | 2+4 | Rename CubeDemo→LevelDemo, player-controller game component/prefab, end-to-end playable gate. |

---

## 4. Global sequencing & gates

Each stage has a **gate** — an objective, demonstrable condition. No stage starts before
its predecessor's gate is green. Stages within a row may run in parallel.

| Stage | Work | Gate |
|-------|------|------|
| **S0** | Reflection boundary **spike** (01-, §Spike). A throwaway `game.so` registers a component + `TypeSchema` into the engine-owned registry across the real DLL boundary; engine round-trips it. | A separately-built `.so` component is registered, added to an entity, queried, and scene-serialized **with no `typeid` reliance** and no symbol-visibility hacks. Decision recorded: proceed with the chosen identity scheme. |
| **S1** | Reflection rewrite (01-) to stable `ComponentTypeId` across the whole engine; all 780+ tests green. | Full test suite green on the rewritten identity path; CubeDemo unchanged behaviorally. |
| **S2** | engine → shared lib; `game` module target; `IGameModule` ABI; `app` host loads a module (02-). | `app` loads a trivial `game.so`, runs a zone authored with a game component, renders. No static game linkage anywhere. |
| **S3** | Editor loads game modules; generic schema-driven inspector + hierarchy; editor places & serializes game components (02-). | In the editor, a game component (defined only in `game/`) appears in the inspector, is editable, saves and re-loads. |
| **S4a** ∥ **S4b** | (a) Brush representation rewrite (03-). (b) LevelDemo rename + player-controller component (06- first half). | (a) Author a clipped/extruded brush; save/load round-trips the mesh. (b) LevelDemo runs a hand-authored level JSON with a player-controller game component. |
| **S5** | Brush texturing + projection UVs (04-). | Apply a material to a face; resize the brush; UVs re-project correctly; round-trips through save/load. |
| **S6** | Level cook: bake to static mesh (sections/material), Generated source kind, cooked-cache + prune (05-). | Cook a brush level → cooked scene + `.smesh`; LevelDemo (no editor symbols) renders it; re-cook mints no new ids; deleting the level + prune removes the artifact. |
| **S7** | PIE in-editor play; player-driven playable end-to-end (05- PIE, 06-). | From the editor, hit Play: brushes render as solid lit mesh, the game's player controller drives, zero `.cooked/` writes. |
| **S8** | Polish: asset bake-out (brush→committed `.smesh`/glTF), error handling, docs (05- §bake-out, 04-, 06-). | A brush selection bakes to a first-class, instanceable `.smesh` asset; docs updated; e2e test under llvmpipe. |

---

## 5. Definition of Done (the gate for the whole branch)

All true, each pinned by a test or a recorded manual verification:

1. A level authored in the editor (brushes + game components, e.g. a player start +
   player controller) **saves** to authored JSON and **re-loads** identically, including
   game components the editor learned about only from the loaded module.
2. The **same level cooks** to a cooked scene + manifest + `.smesh`, loadable by a stock
   runtime host linking **no editor symbols**, rendering identical geometry.
3. A game programmer adds a **new component in `game/` only** and it serializes, inspects,
   and cooks in both editor and runtime — **no engine or app edits**.
4. The game module is **loaded at runtime** by both editor and host; the same artifact,
   not relinked per tool.
5. Re-cooking an unchanged level **mints no new asset ids/paths**; deleting a level +
   prune removes its generated geometry and cache entry. No garbage, audited.
6. Brushes support **clip, extrude, face-delete**, **per-face materials**, and **UVs that
   survive resize**.
7. **PIE**: editing a brush and pressing Play shows solid lit geometry driven by the
   game's player controller, with zero asset disk writes.
8. Reflection uses **module-stable component identity**; no code path keys component
   lookup on `typeid` across a module boundary. (Audited by grep + the boundary test.)

---

## 6. Conventions (binding on every doc in this suite)

Inherited from `docs/SenchaEditor.md` and the engine, restated so they are not re-derived:

- **Global namespace.** No enclosing namespaces. `PascalCase` types/files/methods/members.
  No `m_`. Interfaces prefixed `I`.
- **The engine never includes editor headers.** Editor → engine only. The new rule this
  suite adds: **the engine never includes any specific game's headers**, and **game/editor
  never host engine-global state** (registries, the asset front door) — they call into the
  engine's single instance.
- **Pure where it counts.** Cook/bake/geometry functions are pure (no logging, no threads,
  no ambient state), tested with zero threads — the pattern the asset pipeline already
  proved (`docs/assets/pipeline.md` Decisions B, C).
- **Deferral needs a trigger.** Anything deferred is recorded with the concrete condition
  that revives it.
- **Components are trivially copyable** (archetype chunks memcpy rows — see
  [World.h:92](../../engine/include/ecs/World.h)). This constrains what a game component
  may contain; the prefab/asset-ref story in 02-/05- respects it.

---

## 7. Glossary

- **Brush** — an editor-only, editable polygonal solid; the authoring primitive for level
  geometry. Never a runtime concept. Replaces today's box `BrushComponent`.
- **Cook** — the derive step that turns authored data (level JSON, brushes) into runtime
  artifacts (`.smesh`, cooked scene, manifest). Lives under `.cooked/`, never committed.
- **PIE** — Play-In-Editor: in-memory bake + run, no disk artifacts.
- **Game module** — the runtime-loaded library (`game.so`/`game.dll`) holding a title's
  components, prefabs, and systems; registered through `IGameModule`.
- **`ComponentTypeId`** — the new module-stable component identity replacing `typeid`
  as the registry key (01-).
- **Authored vs Cooked** — authored data is human-edited and version-controlled; cooked
  data is derived, reproducible, and disposable. The boundary is sacred.
- **Projection UV** — per-face UVs defined as a world/face-space projection (axis, scale,
  offset, rotation), not baked coordinates — so resizing re-projects for free (04-).

---

## 8. Open product questions (escalate before the owning stage)

- **Carving (boolean subtract)** — stretch only. The brush model (03-) must not *preclude*
  it, but we do not commit to it this branch. Trigger to revisit: a level that genuinely
  needs concave subtraction the poly tools can't express.
- **True prefab *assets*** (reusable, parameterized, instanced) vs. **component-on-entity**.
  This branch ships the player controller as a game component on an entity serialized into
  the level (cheapest correct thing). Prefab-as-asset is forecast (pipeline.md Decision O);
  trigger: the first content need for a shared, overridable instance.
- **Collision for "playable."** v1 may ship without brush collision (player flies / or a
  trivial ground plane). Trigger: a design that needs the player to stand on brush geometry
  → collision-shape cook (pipeline.md Decision O), a sibling output of the same cook step.
- **Hot-reload of the game module** (reload `game.so` without restarting the editor) — the
  DLL makes it *possible*; we build the loader so it is *not precluded*, but do not ship
  live reload this branch. Trigger: gameplay-iteration pain on a real title.
