# Pillar 2+4 — LevelDemo, the Player-Controller Game Module, and the Playable Milestone

**Status**: Working plan (2026-06-14). Depends on 02- (module topology) for the module shape
and 05- (cook/PIE) for "playable." **Spans Stages S4b (rename + player component) and S7/S8
(end-to-end playable).**

> `CubeDemo` is renamed/refactored into **`LevelDemo`**: the engine's dogfood **game module**
> plus a host invocation. It is *proof the whole chain works* — author → save → cook → load →
> play — not the development target. The player controller is the first real **game component**,
> and it is the concrete test of "the editor picks up game-defined components."

---

## 1. What LevelDemo becomes

Today `CubeDemo` is a self-contained exe linking the engine and hard-coding its scene/systems
([example/CubeDemo/](../../example/CubeDemo/)). After 02- it splits into:

- **A game module** (`game/leveldemo/` building `leveldemo.so`/`.dll`) containing the
  title-specific components, prefabs, and systems — most importantly the **player controller**.
  Implements `IGameModule` (02-§2); its one exported `SenchaCreateGameModule` returns a
  `LevelDemoModule`.
- **A host invocation**: the generic `app` host (02-§4) launched with `--game leveldemo --zone
  <cooked level>`. No bespoke `main` per demo; the demo *is* config + the module.

What carries over from CubeDemo: the free camera / player feel, the async zone load, the
hot-reload wiring — but re-homed: engine-generic parts stay in the engine/host; title parts
move into the module.

> Other examples (`AudioTest`, benchmarks) are evaluated case-by-case: pure engine benchmarks
> stay as engine-linked exes (they test the engine, not the game-module path). Only the
> *game-shaped* demo (CubeDemo→LevelDemo) becomes a module, because it is the one proving the
> shipping topology.

---

## 2. The player controller as a game component (+ prefab)

```cpp
// game/leveldemo/PlayerController.h  (in the game module — NOT engine, NOT editor)
struct PlayerController {
    float MoveSpeed   = 5.0f;
    float LookSpeed   = 0.15f;
    float EyeHeight   = 1.7f;
    // trivially copyable (World requires it). No vectors/strings inline.
};

template <> struct TypeSchema<PlayerController> {
    static constexpr std::string_view Name = "leveldemo.player_controller";  // → ComponentTypeId
    static auto Fields() {
        return std::tuple{
            MakeField("move_speed", &PlayerController::MoveSpeed).Default(5.0f),
            MakeField("look_speed", &PlayerController::LookSpeed).Default(0.15f),
            MakeField("eye_height", &PlayerController::EyeHeight).Default(1.7f),
        };
    }
};
```

- **Identity**: the schema name `"leveldemo.player_controller"` is the single source of the
  `ComponentTypeId` (01-), the JSON key, and the human label. The `vendor.name` convention
  (`leveldemo.*`) keeps it collision-free from engine and other titles.
- **Registration**: `LevelDemoModule::Register(ctx)` calls `ctx.Serializers.Register<PlayerController>()`
  (storage + serializer) and registers a **prefab** `"leveldemo.player_start"` =
  `{ LocalTransform, PlayerController }` so designers place it as one named thing (02-§6).
- **System**: a `PlayerControllerSystem` (movement/look from input) lives in the module and is
  registered via `ctx.Systems`. The host runs it; the editor runs it only under PIE (02-§5.2,
  05-§7).

### 2.1 The editor picks it up — the whole point

With the editor loading the LevelDemo module (02-§5):
- The **prefab palette** shows "Player Start"; placing it issues `InstantiatePrefabCommand`.
- The **inspector** shows a "Player Controller" section with move/look/eye-height fields —
  drawn by the generic schema path (02-§5.3), with **zero editor code naming
  `PlayerController`**.
- **Save** writes the component into the level JSON under `"leveldemo.player_controller"`;
  **re-load** restores it. The editor learned the type entirely from the loaded module.

This is the DoD item "a programmer adds a component in `game/` only and it inspects/serializes/
cooks in both tools" (00-§5.3), made concrete on the first real component.

---

## 3. The level → cooked → played chain

1. **Author** (editor): brushes (03-/04-) + a Player Start prefab placed on the floor.
   Save → authored JSON + brush sidecar + the player-controller component instance.
2. **Cook** (05-): brushes → `asset://levels/<stem>.geometry.smesh` (sections per material);
   cooked scene with the `StaticMeshComponent` entity + the **pass-through player-controller
   component**; manifest (incl. face materials).
3. **Load + play** (host): `app --game leveldemo --zone <cooked>` — the host loads the module
   (registers `PlayerController` + its system), `AsyncZoneLoader` streams the level's assets
   (mesh + materials + textures) via the manifest, attaches the zone; the player-controller
   system drives the camera over the brush-built geometry.
4. **Or PIE** (05-§7): same, in-editor, in-memory, no disk.

---

## 4. Stages & gates

**S4b — rename + player component (can run alongside S4a brush work):**
- Extract `game/leveldemo/` module; `LevelDemoModule : IGameModule`; `PlayerController`
  component + schema + prefab + system.
- Host runs a **hand-authored** level JSON (no cook yet) using the module: player controller
  drives over a placeholder floor.
- *Gate:* `app --game leveldemo --zone <hand-authored>` loads the module and the player
  controller moves the camera; the component round-trips through `SaveSceneJson`/`LoadSceneJson`.

**S7/S8 — end-to-end playable (the branch headline):**
- Full chain §3 with real brush geometry and cook.
- *Gate (the branch DoD demonstrated):*
  1. Author a brush level + Player Start in the editor; save.
  2. Cook; LevelDemo host (no editor symbols) loads and renders it; the player controller
     drives over the brush geometry; per-face materials show.
  3. PIE: Play in-editor shows the same, zero disk writes.
  4. Re-cook mints no new ids; deleting the level + prune removes its generated mesh.
  5. The player-controller component was defined only in the module, picked up by the editor,
     and passed through the cook unchanged.

---

## 5. Risks & mitigations

- **Collision for "stand on the floor."** v1 may have the player fly (free-cam feel) or stand
  on a trivial ground plane while brush collision is deferred (00-§8). The playable gate is
  *render + drive*, not *physically collide*, unless collision lands. Mitigation: state the
  player-feel explicitly in the gate; collision-cook is a sibling output (pipeline.md
  Decision O) when needed.
- **Editor running game systems under PIE** could crash on buggy game logic. Mitigation: PIE
  is opt-in and tears down to a clean editor on stop; authoring never ticks game systems.
- **Demo gravity** — the temptation to special-case LevelDemo in engine/editor. Forbidden: if
  something LevelDemo needs isn't expressible through the module ABI, the ABI is wrong and we
  fix the ABI, not add a demo carve-out. LevelDemo is a *consumer* of the same surface a third
  party uses.
- **Other examples** depending on the old CubeDemo shape — audit `example/` for references to
  CubeDemo targets/paths during the rename; update or retire.

---

## 6. Definition of done check (this doc's slice)

- [ ] `game/leveldemo` builds as a loadable module implementing `IGameModule`.
- [ ] `PlayerController` is a module-only component with schema, prefab, and system.
- [ ] Editor loads the module; player controller inspects/places/serializes with no editor
      code naming it.
- [ ] Host (`app --game leveldemo`) loads the module and plays a cooked brush level.
- [ ] PIE plays the same in-editor with zero disk writes.
- [ ] No engine/app/editor edits were needed to support the *specific* `PlayerController`
      type — only the generic module/registry surface.
