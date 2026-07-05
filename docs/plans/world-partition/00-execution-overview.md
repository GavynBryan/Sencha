# World Partition Execution Suite: Overview and Guardrails

Status: execution spec (2026-07-02). Companion to `docs/plans/world-partition-authoring.md`
(the design document; it owns the model and the phase definitions). This suite owns
implementation detail at the standard set by `docs/plans/sencha-level-editor/00-overview.md`:
detailed enough that two people working from it independently would build the same thing.
The roadmap (`docs/plans/engine-roadmap.md`) owns versions and gates.

Audience: the implementer of any world-partition phase, assumed competent but not assumed
to make architecture calls. Where a decision could go two ways, this suite picks one and
says so. If a situation arises that this suite does not cover, the correct move is to stop
and ask, not to improvise (see "Stop conditions" below).

---

## 1. Document map

| Doc | Phase (design doc Section 7) | Specifies |
| --- | --- | --- |
| `00-execution-overview.md` | all | Guardrails, pinned decisions, sequencing, stop conditions. |
| `01-manifest-and-identity.md` | Phase 1 | `ZoneId` migration, partition ids, manifest records, JSON schema, adjacency index, validation, tests. |
| `02-world-document-and-partition-tree.md` | Phase E1 | `WorldDocument`, workspace surgery, zone editor states, tree panel, bounds overlay, world cook. |
| `03-runtime-streaming.md` | Phase R | `WorldPartitionRuntime`, demand policy, streaming tunables, template game world path, PIE play-from-world. |
| `04-move-selection-to-zone.md` | Phase E2 | Cross-zone entity moves with undo, UI entry points, bounds-containment validation. |
| `05-transitions-and-portals.md` | Phase E3 | Portal marker brushes, transition verbs and panel UI, linkage validation, cook exclusion. |
| `06-streaming-maturation.md` | Phase N | Demand-model extensions (render-only neighbors, spatial radius, tag-gated transitions, per-edge preload depth). IMPLEMENTED 2026-07-05. |
| `07-global-content.md` | Phase G | The world scene: authored global content loaded once into ZoneRuntime::Global(). IMPLEMENTED 2026-07-05. |
| `08-context-zone-rendering.md` | Phase V | Context zones with real materials under a grey overlay; flat portal fill. Spec only; owner review before implementation. |
| `09-retire-portals-doors-as-world-content.md` | (reversal) | Portals removed entirely; connections authored zone-to-zone only; doors recorded as future world-scene content. Reverses D9, D15, D19, D20. IMPLEMENTED 2026-07-05. |
| `10-per-region-streaming-and-topology-labels.md` | (streaming shape) | Per-region streaming overrides (hop, radius, cap) resolved by focus region; honest topology labels. IMPLEMENTED 2026-07-05. |

Execution order: Phase 1 first, alone, to completion. Then E1. After E1, Phase R and
Phase E2 may proceed in parallel (separate lanes: R never touches the editor, E2 never
touches the engine). Phase E3 starts after E2; it builds on E2's cross-zone command
precedents and the D12 unload pin.

---

## 2. Binding rules (the failure modes this suite exists to prevent)

Every rule below is a restatement of CLAUDE.md or the roadmap, made concrete for this
work. Violating one is a defect even if the code works.

Architecture:

1. **No new abstractions.** This suite defines zero interfaces, zero strategies, zero
   virtual seams. If you find yourself writing `class I...` or a second implementation
   slot "for later", stop. `IZonePopulationStrategy` is explicitly deferred by the
   roadmap; do not build it, or anything shaped like it.
2. **No policy in `ZoneRuntime`.** `ZoneRuntime` owns registries and participation. It
   gains nothing in any phase of this suite. Phase R's policy layer sits above it.
3. **The engine never includes editor headers. Editor code never hosts engine-global
   state.** The manifest types live in the engine (`engine/include/zone/`); the editor
   includes them, never the reverse. `scripts/check_editor_layering.sh` must stay green.
4. **No locks, no raw threads, no `std::async`.** Nothing in Phases 1 and E1 is
   concurrent. If you think you need a mutex, you have the design wrong; stop.
5. **No grab-bag names.** No `Manager`, `Helper`, `Util`, `Handler`. No genre words, no
   project codenames, no "intent" words in identifiers. The vocabulary is fixed by the
   design doc: World, Zone, Region, Transition, Portal, Space. Content names ("Chozo
   Ruins") appear only as string data in fixtures.

Data and determinism:

6. **Ids are `StrongId`.** Never pass a raw `uint64_t`/`uint32_t` where a partition id
   exists. Zero is invalid, always.
7. **JSON: snake_case keys; 64-bit ids as 16-digit lowercase hex strings** (JSON numbers
   are doubles and cannot hold 64 bits; the `AssetId` and cooked-cache precedent).
8. **Deterministic outputs.** Anything that emits a list (adjacency indices, validation
   records, cooked manifests) emits it in a defined order (stated per structure in the
   specs). Never iterate an unordered container into an output.
9. **The engine mints no random ids.** Id minting is editor-side only (Section 3, D4).
   Engine runtime code stays free of unseeded randomness.

Process:

10. **One stage per commit, suite green between stages.** Every stage in these specs ends
    with the full ctest suite green (roughly 852 tests at last count, plus what each
    stage adds). A stage that cannot land green is mis-scoped; stop and say so.
11. **Tests ship with the mechanism.** Each stage's spec lists its tests by name. Write
    them; they are the gate, not an afterthought.
12. **No dead code.** No fields parsed but never read, no reserved-for-later members, no
    half-wired UI. If a spec says a field is deferred, it does not appear in code at all.
13. **No em dashes** in code, comments, docs, or commit messages. Comments explain why,
    not what.

---

## 3. Pinned decisions

These close the open questions an implementer would otherwise have to guess at. Each is
final for v1.0 unless the owner overrides it on the record.

**D1. Portal linkage is content-side and one-directional.** The manifest never
references an entity. A portal is an entity in its source zone's content whose component
stores the `TransitionId` it realizes. `TransitionRecord` carries no portal field.
Validation resolves the linkage only when the source zone is loaded; until then the rule
reports "unverifiable until loaded", which is a visible state, not a silence. This keeps
the manifest strictly O(zones + transitions) and removes any dependency on a stable
entity identity scheme (Track C item 5) from every v1.0 partition phase. Do not invent
an entity reference type for the manifest; the design doc has been amended to match.

**D2. `ZoneId` migrates to `StrongId<struct ZoneIdTag, uint64_t>`,** replacing the
hand-rolled `uint32_t` struct in `engine/include/zone/ZoneId.h`. `ZoneId::Invalid()`
call sites become `ZoneId{}` (there are exactly two outside tests; `01-` lists them).
The hand-rolled `std::hash<ZoneId>` specialization is deleted (StrongId provides one).
Existing `ZoneId{ 1 }` literals keep compiling. Details and the full call-site checklist:
`01-manifest-and-identity.md` Section 3.

**D3. Exactly one editable zone at a time (the focus zone).** `EditableInEditor` is a
per-zone flag, but the workspace enforces at most one true. Tools, the manipulation
sink, mesh editing, selection, and undo all continue to assume a single active document;
they bind the focus zone. Context zones are render-and-reference only. This is the
single largest risk reducer in the editor work: nothing in the existing tool stack
becomes multi-registry. Revisit trigger: a designer demonstrably needs simultaneous
brush edits across two zones.

**D4. Id minting is editor-side.** Kyusu mints partition ids (zones, regions,
transitions) as random nonzero 64-bit values via `std::random_device` seeding
`std::mt19937_64`, re-rolling on zero and on collision with any id already in the
manifest. Minting lives in the world-document code, never in the engine.

**D5. Focus change resets interaction state through the same path as document open.**
Changing the focus zone tears down and rebuilds everything that may hold
focus-document references (tool context, manipulation sink, sessions, pivot, marquee,
overlay, selection) and clears the undo stack, exactly as `ResetEditorState` does on
Open today. Cheap, safe, and impossible to get subtly wrong. Recorded trigger to
revisit (preserving undo across focus changes): designer pain in practice.

**D6. Phase 1 owns the first `ContentRiskRecord`.** Track C item 1 (the streaming
records) has not landed; rather than block on it or invent a parallel record, Phase 1
defines a minimal `ContentRiskRecord` in `engine/include/zone/ContentRiskRecord.h`
(shape pinned in `01-`, Section 5). If Track C item 1 lands first, use its type and
delete this decision; coordinate, do not duplicate.

**D7. The world cook is part of Phase E1 (its final stage), not Phase R.** Phase R may
start against hand-written cooked manifests (its spec will include the fixture); it
must not block on editor work. The cook stage in `02-` produces the real thing.

**D8. Scene references in the manifest are project-relative path strings**
(`std::string`), not `AssetRef`/`AssetPath`, in v1.0. Zone scenes are documents, not
assets in the front-door sense; entangling them with the asset system now buys nothing.
Revisit trigger: binary cooked scenes (Track F) making cooked zone scenes first-class
assets.

**D9.** Reversed by `09-retire-portals-doors-as-world-content.md` (portals removed).

**D10. The zone content recipe is a game-supplied function.** `WorldPartitionRuntime`
decides which zones are resident; the game decides how a zone's cooked refs become a
registry. The seam is one callable, `ZoneLoadRecipeFn`, returning per-zone build and
finalize callbacks plus an optional asset preload (exact shape in `03-`). This is the
game-binary boundary; it is a function, not an interface, because one implementation
slot is all the boundary needs.

**D11. Transition manifest edits are non-undoable; portal linkage is undoable.**
`AddTransition`/`RemoveTransition` and the transition setters are `WorldDocument`
verbs, off the `CommandStack` like `AddZone` (the E1 W4 precedent). Setting a portal's
transition field is an ordinary undoable component edit. Consequence, stated plainly:
undoing a link can leave an orphan transition record; validation reports it and removal
stays explicit. One undo system; no manifest snapshot machinery invented.

**D12. `UnloadZone` clears the undo stack.** A cross-zone move command on the stack
references two documents; unloading the non-focus one would dangle it. `WorldDocument`
gains an `OnZoneUnloaded` observer; the workspace binds it to clearing the
`CommandStack` (narrower than the full D5 reset: focus did not change, so tools and
selection stay). Chosen over refusing unload while the stack is non-empty, which would
make unload effectively unusable.

**D13. Portals are editor-only in v1.0; the cook strips them.** Portal brushes
contribute nothing to render cells, the collision bake, or the cooked passthrough
scene (the `BakedBrushComponent` precedent). Phase R streaming consumes transitions,
never portals; a cooked portal component today would be a parsed-but-never-read field.
Recorded trigger to emit an engine-side runtime component: Track C item 6 (the
transition timing model) defining what the runtime actually needs from a portal volume.

**D14. Phase R tunables are `EngineRuntimeConfig` fields** beside `AsyncCommitBudgetMs`:
`StreamingHopCount` (int, default 1), `StreamingLingerSeconds` (double, default 3.0),
`StreamingResidentZoneCap` (int, default 8); JSON keys `streaming_hop_count`,
`streaming_linger_seconds`, `streaming_resident_zone_cap`; validated as hop >= 0,
linger finite and >= 0, cap >= 1.

**D15.** Reversed by `09-retire-portals-doors-as-world-content.md` (portals removed).

**D16. Transitions carry an optional authored `Name`** (manifest key `name`, omitted
when empty; format_version stays 1). Empty displays as the derived
"<From name> -> <To name>"; `TransitionDisplayName` is the one label mechanism, and
every editor surface presents the link as WORLD-level data, never zone-local.

**D17 (owner decision, 2026-07-05). Render-only neighbors are the pinned direction**
for preloaded zones: neighbors preload with Visible (and, for threshold safety,
Physics) participation instead of dormant, flipping full on entry. Retires the R
spec's "accepted pop". Specced in `06-streaming-maturation.md` N1; not implemented
until that spec is reviewed.

**D18. The editor streaming preview consumes ONLY the pure policy**
(`ResolveFocusZone`, `ComputeZoneDemand`): no `ZoneRuntime`, no loader, no async
anywhere in kyusu. Focus resolution therefore lives beside the demand policy in
`zone/ZoneDemand.h`, shared by the runtime and the preview.

**D19.** Reversed by `09-retire-portals-doors-as-world-content.md` (connections are
authored zone-to-zone via `ConnectZones`, never derived from geometry).

**D20.** Reversed by `09-retire-portals-doors-as-world-content.md`; the undirected
one-row-per-pair display it pinned survives unchanged (restated as P-D2 there).

---

## 4. Stop conditions

Stop, write down the situation, and ask the owner (do not pick silently) when:

- A stage seems to require touching `ZoneRuntime`, `AsyncZoneLoader`, or
  `ZoneParticipation` in Phases 1 or E1. Neither phase touches them at all.
- You want an interface, base class, or callback with one implementation.
- A JSON field, record field, or UI element seems needed that the spec does not list.
  The specs enumerate fields exhaustively on purpose; a missing one is a design
  question, not an oversight to patch.
- Undo, selection, or focus semantics arise that Section 3 (D3, D5) does not answer.
- The suite cannot be kept green between two stages as scoped.
- Any name you are about to introduce contains Manager/Helper/Util/Handler, a genre
  word, or a project name.
- You are about to add a second way to do something that has one way (a parallel flag
  system, a side-channel loader, a second id scheme).

The design doc's Section 0 verdicts and Section 11 deferrals are binding context; if an
idea appears there as rejected or deferred, it stays that way regardless of how natural
it feels mid-implementation.

---

## 5. Definition-of-done template

Every stage in the phase specs ends with this checklist, plus its own gate:

1. Full ctest suite green, including the stage's new tests.
2. `scripts/check_editor_layering.sh` green (editor stages).
3. No new compiler warnings in touched targets.
4. Grep audits listed by the stage pass (each stage names its own).
5. No TODO, no commented-out code, no dead fields introduced.
6. Commit message states the stage id (for example "partition 01/S2: manifest records
   and JSON round-trip") and contains no em dashes.

---

## 6. Pinned early for phases not yet specced

So that Phases 1 and E1 do not build anything these later phases would have to undo:

- **Phase R:** specced in `03-runtime-streaming.md` (supersedes the early pins that
  lived here: policy type beside `ZoneRuntime`, config-field tunables per D14, unload
  is `DestroyZone` until stateful detach lands).
- **Phase E2 (Move Selection To Zone):** specced in `04-move-selection-to-zone.md`
  (supersedes the early pins that lived here: `CaptureEntity`/`RestoreEntity` across
  two zone documents, sidecar moves with the entity, target must already be loaded).
- **Phase E3 (transitions and portals):** specced in `05-transitions-and-portals.md`
  under D9: the portal is a marker volume brush flagged by a component that stores
  only the linked `TransitionId` (D1), trivially copyable (chunk memcpy constraint,
  no strings, no vectors); shape and normal derive from the brush geometry. There is
  no opening-cut verb in this phase; the designer cuts openings with the existing
  mesh tools.
