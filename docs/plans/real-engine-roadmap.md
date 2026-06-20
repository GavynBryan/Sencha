# Sencha: From Substrate to Engine

Roadmap to harden Sencha from a strong technical substrate into a versioned engine
product a game developer can ship a game on without ever touching engine source.

## Thesis

Sencha today is a strong engine substrate with a toy-level product surface. The
substrate is real: an archetype ECS, two-lane concurrency, a Vulkan renderer and
render pipeline, world partition and async zone streaming, a content-hashed asset
cook, and (as of the v4 work) a game module that IS a `Game` loaded across a
hardened ABI. What is missing is everything that turns a substrate into an engine:
a consumable SDK boundary, an editor that drives the full author -> cook -> play
loop, gameplay runtime frameworks, a shipping pipeline, and production hardening.

The two target games are the forcing function (see [[target-games]]): Loss Function
(FPS-Metroidvania) and a 3rd-person Zelda-like. Every framework below is a
shape-neutral mechanism those two games CONFIGURE. The genre is data, never a type.

## Invariants this plan does not violate

The CLAUDE.md prime directives hold throughout: name mechanisms not intents,
behavior from data not branches, earn every abstraction, two concurrency lanes,
determinism (serial == parallel), editor/cook code never links into the runtime.

One invariant is added and made load-bearing here:

- **The engine, editor, and runtime are a versioned binary product. A game
  developer never rebuilds them.** The published contract is the v4 module ABI
  (the fingerprint handshake) plus the cooked-content formats. Anything that would
  force a game developer to recompile the engine to change gameplay is a defect.

## Where we are (the honest starting line)

Done and load-bearing:
- ECS: archetype SoA chunks, CommandBuffer structural changes, `Changed<T>`, cached
  queries, `ComponentTraits` lifecycle.
- Concurrency: JobSystem fork-join + AsyncTaskQueue, the `worker_count == 0`
  deterministic reference path.
- Renderer: Vulkan, DefaultRenderPipeline, static/skinned mesh over shared
  MeshGeometry, `.smat`/`.stex` materials and textures.
- World: Registry/ZoneRuntime, WorldPartitionRuntime, async zone load with
  ZoneParticipation, gameplay tags.
- Assets: `IAssetLoader` staged load, content-hashed CookedCacheIndex, AssetIdMap,
  AssetManifest, `ScanAssetsDirectory` + `.cooked/` overlay, mesh/texture/blend/audio
  importers, and the S6 level cook (brush -> per-cell static mesh + cooked scene).
- Module boundary (v4): `Game*`-returning module factory, ABI fingerprint refusal,
  `GameModuleLoader`, the `app` pure loader, `Game::OnRegisterComponents` (editor
  borrows serializers without running the game), the `SceneViewer` default module.
- Editor: level/brush/mesh editing, selection, undo/redo, loads a game module for
  its component serializers.

Toy-level or missing (the gap to "real engine"):
- No SDK boundary: a game cannot be built out-of-tree against prebuilt binaries.
- No editor product loop: no project concept, no Cook button, no Play/Stop, no
  inspector for game-defined components.
- No gameplay runtime: no physics/collision, spatial queries, character controller,
  or input action mapping. This is the greenfield gap and the critical path
  ([[gameplay-runtime-gap]]).
- No shipping pipeline: cooked content is JSON only, cook is not incremental, there
  is no prefab/factory system, and there is no package step or build configurations.
- Thin production hardening: malformed/old/missing content is not gracefully
  handled, there is no scene version migration, no gameplay save system.

## The plan

Each phase ends at a demonstrable gate. Phases are ordered by the critical path to
the two target games, not by ease.

### Phase 0 - The SDK boundary: a consumable engine

Goal: a game developer builds only their `game.so` + content against prebuilt
engine binaries, and never rebuilds the engine or editor.

- `install()` / export the product: `libsencha_engine.so`, `app`, `sencha_editor`,
  and the public module-facing headers (the same surface the ABI fingerprint
  hashes) into an install tree.
- `SenchaConfig.cmake` so an out-of-tree project does `find_package(Sencha)` and
  gets an imported engine target plus a `sencha_game_module(<target>)` helper that
  builds a MODULE with hidden visibility and the ABI export wired in.
- An engine SDK version (distinct from the per-build ABI fingerprint) recorded in
  the install, so a module can state which engine it targets.
- A game-project template: an out-of-tree directory with `find_package(Sencha)`,
  one game-module target, and `assets/`. It builds with zero engine source present.
- The project concept: a project descriptor (name, game-module path, content roots)
  the editor opens; the editor loads that project's `game.so` for serializers and
  PIE spawns `app` against it.

Gate: clone the template outside the repo, with only installed Sencha binaries and
headers present, build `game.so`, open it in the prebuilt editor, and Play it in the
prebuilt `app`. No engine rebuild occurs anywhere in that loop.

### Phase 1 - The editor product loop: author -> cook -> play

Goal: the editor is a tool you build a level with and immediately play.

- Project open / create / save (the descriptor above).
- Cook Level button + a cell-size cvar + a `CookLevel` overload that cooks the live
  (possibly unsaved) document, not just a file on disk.
- Play / Stop: spawn `app --game <project game.so> +map <cooked level>` with the
  project as CWD; Stop kills the process. This is the already-decided out-of-process
  PIE ([[pie-architecture-decision]]); PIE == the shipping path.
- Component inspector: reflect game-defined components from the loaded module via the
  existing `TypeSchema`/`Field` metadata (the same reflection the serializers use),
  so a designer edits game component values with no engine code.
- Entity/prefab authoring beyond brushes: place and configure entities, persisted
  into the level scene.

Gate: open a project, build a level (brushes plus placed entities carrying game
components), cook, Play, watch the game's systems run on it, Stop, and iterate
without restarting the editor.

### Phase 2 - Gameplay runtime foundations (the critical-path gap)

Goal: a controllable character moving on cooked world geometry, with both target
control schemes expressible as data over shared systems.

- Collision + physics: a broadphase over the partition, narrowphase against cooked
  collision geometry (a cooked sibling of the render mesh), and rigid/character
  sweeps. This is the known bottleneck ([[gameplay-runtime-gap]]).
- Spatial queries: raycast / shapecast / overlap as engine services, used by
  gameplay, AI, and the editor pick path alike.
- Character controller: one capsule-controller system. FPS and 3rd-person are
  component and config differences, not two controllers.
- Input action mapping: data-driven device -> action bindings consumed by gameplay
  systems. No hardcoded scancode checks in game code.

These ship in the engine as systems and components a game module composes; the
genre stays configuration.

Gate: in the template game module, a player capsule walks, jumps, and looks on a
cooked level using mapped input and collides with the geometry, in both an FPS and a
3rd-person camera configuration, with no engine change between the two.

### Phase 3 - Content and shipping pipeline

Goal: cook and package a standalone shippable build.

- Binary cooked formats behind the existing format-neutral archive seam
  (`SceneFieldCodec` already branches `IsText()`; `SaveSceneBinary`/`LoadSceneBinary`
  exist). Flip cooked content to binary, keep JSON for authoring.
- Incremental cook: a dependency graph over sources so only affected artifacts
  re-cook (CookedCacheIndex is the seed).
- Prefabs / factories: entity templates as cooked content with instanced spawning
  (named as future in the module context; build it when Phase 1 authoring needs it).
- Packaging: a path that produces a bundle (app + game.so + cooked content, no
  editor or cook code) and build configurations (dev vs shipping; shipping strips
  cook, the dev console, and asserts).

Gate: package a game into a directory that contains no engine source and no editor,
and it runs the cooked game standalone.

### Phase 4 - Production hardening

Goal: it does not fall over on real, messy, evolving content.

- Graceful degradation: missing / malformed / stale cooked content logs and
  substitutes a placeholder, and never crashes the runtime.
- Content version migration: a versioned cooked format with upgrade-on-load.
- Determinism gates in CI: serial == parallel checks on the gameplay systems, and the
  existing fitness functions (module ABI surface, meshedit deps, UI colors) extended
  to the new surfaces.
- Asset hot-reload extended across the dev loop (exists for some types today).
- Save games: gameplay persistence, distinct from scene serialization.

Gate: a corrupt, old, or missing-asset project loads with diagnostics and no crash,
and CI fails on a determinism or ABI-surface regression.

## Cross-cutting tracks

Pulled into the phases above only as the two target games actually demand them, never
speculatively:

- Rendering for the target games: dynamic lighting and shadows, transparency, a HUD/UI
  surface, debug draw, particles.
- Animation runtime: skinned mesh exists; add an animation state machine through the
  `IPoseModifier` seam for the 3rd-person game.
- World streaming at scale: WorldPartitionRuntime exists; validate it under a real
  Metroidvania backtracking load with budgets and manifest data.

## Sequencing rationale

Phase 0 is first because the SDK boundary is the product, and it is cheap now: the v4
module ABI already did the hard part (a prebuilt host loading a separately built game
across a checked boundary). Phase 1 makes the engine usable by a person. Phase 2 is
the longest pole and the real risk (gameplay is greenfield). Phases 3 and 4 turn a
playable thing into a shippable one. The cross-cutting tracks ride along as Loss
Function and the Zelda-like force them, which keeps every feature anchored to a real
requirement instead of a guess.
