# Phase G: Global Content (the world scene)

Status: execution spec (2026-07-05), NOT implemented. Owner review before any stage
starts. Read `00-execution-overview.md` (especially D19/D20 and the R5a global-span
decision) first.

## The model

The runtime already has the "world-lifetime" container: `ZoneRuntime::Global()`, the
registry that participates in every frame span and survives all streaming (the pawn
and camera live there today). This phase gives it an AUTHORING surface: one **world
scene** per world, edited like a zone, loaded once into the global registry at world
start, never streamed.

Naming: it is the **world scene / global content**, never a "master zone" (owner
decision): it has no bounds, no transitions, no participation, and calling it a zone
would re-blur the line between streamed space and world-lifetime state.

What belongs in it: world-lifetime DATA entities. The player start marker, global
volumes, skybox/ambient carriers, persistent props, and authored state components
that world-lifetime systems read (see the directors note below). What does not: heavy
content that only matters somewhere (zone content), and logic (systems).

**Directors note (combat/AI managers).** Sencha systems are concrete classes and MAY
hold state (PhysicsStepSystem owns the simulation; that is the norm, not an
exception); shared or inspectable state lives in registry RESOURCES (the
AbilityActivationQueue pattern). A crowd director is a system that counts NPCs
across the Logic spans and writes a pressure resource other systems read. What the
world scene adds is the AUTHORED third of that split: a `crowd_tuning` component on
a global entity that designers edit in kyusu and the director reads at world start.
Logic in systems, live state in resources, authored state in global entities.

## Stages (one commit each, suite green, layering script green)

### G1. WorldDocument grows the world scene

- `.sworld` gains optional `"world_scene": "levels/<stem>_world.level.json"` (reader
  tolerant when absent; format_version stays 1). `WorldDocument` owns one extra
  `EditorDocument` for it (open whenever the world is open; never in `OpenZones_`;
  no `ZoneViewState`, no bounds derivation, not reachable by Move To Zone).
- Focusable exactly like a zone (`SetFocusZone(ZoneId{})`? No: a dedicated
  `FocusWorldScene()` plus `IsWorldSceneFocused()`, because it has no ZoneId and
  must never enter the manifest, the index, or the demand policy). The D5 reset
  fires on focus change as for zones.
- Save writes it beside the zone scenes; the sidecar records world-scene focus.
- Tests in `test/editor/WorldDocumentTests.cpp`: round-trip, focus, save, absent-key
  legacy worlds keep loading.

### G2. Panel and cook

- The World panel shows a **World** row above the regions (globe icon, focusable,
  renamable NO: the world scene has no name of its own; the row shows the world's
  name). No bounds badge, no eye toggle (always conceptually present).
- `CookWorld` cooks it through the same `CookDocument` path into
  `worlds/<world>/world.cooked.json` (+ collision), recorded in the cooked manifest
  as `"cooked_world_scene"` fields beside the zone entries.
- Tests in `test/level_cook/WorldCookTests.cpp`: cooked artifacts exist, recook
  byte-identical, world-scene edit changes only its own hash.

### G3. Runtime load and the player start

- Template `world <name>`: after `LoadManifest`, if the cooked manifest names a
  world scene, load it SYNCHRONOUSLY into `Global()` (build+finalize on the global
  registry directly; a world load is a loading-screen boundary, not a streaming
  moment) before seeding focus.
- New editor-and-runtime marker component `player_start` (a Transform carrier, the
  SpinComponent registration precedent, defined game-side in the template):
  `SpawnPlayerAvatar` uses the first `player_start` in the global registry and
  falls back to the current hardcoded position with a log line.
- Tests: template compiles headless (module tests); the runtime half of the load is
  covered by a WorldPartitionRuntimeTests-style fixture only if it lands engine-side
  (it should not: loading global content is game code, like zone recipes).

### Non-goals

Per-entity preload tiers (vista/boundary content: the recorded next mechanism for
doors and LOD proxies resident before their zone), any second global registry, any
"manager entity" convention (directors are systems + resources; see the note).
