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
| (not yet written) | Phases R, E2, E3 | Written before those phases start. Their already-pinned decisions are in Section 3 so nothing built earlier drifts. |

Execution order: Phase 1 first, alone, to completion. Then E1. Phases R and E2/E3 get
their specs written (and reviewed) before implementation starts; do not begin them from
the design doc alone.

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

Every stage in `01-` and `02-` ends with this checklist, plus its own gate:

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

- **Phase R:** the policy layer is a new type beside `ZoneRuntime` (working name
  `WorldPartitionRuntime`, per the roadmap); its tunables are `EngineRuntimeConfig`
  fields following the `AsyncCommitBudgetMs` pattern; unload is `DestroyZone` until
  stateful detach lands. Nothing in Phase 1's manifest types may assume loading
  machinery (they are plain data plus pure functions; keep them that way).
- **Phase E2 (Move Selection To Zone):** built on `CaptureEntity`/`RestoreEntity`
  across two zone documents inside one `CompositeCommand`; the brush mesh sidecar entry
  moves with the entity. No new snapshot machinery. The target zone must be loaded;
  loading it is a prerequisite step the command refuses to do implicitly.
- **Phase E3 (transitions and portals):** the opening cut is a new pure `BrushOps` verb
  `PierceFaceRect` with the domain already recorded in the roadmap (rectangular-quad
  opposite-face pairs); the portal component stores `TransitionId` (D1); the transition
  tool follows the `FaceCarveTool` structure (hover validate, drag preview via the
  sink, commit as one composite command). Portal component data is trivially copyable
  (chunk memcpy constraint): id plus a rect frame, no strings, no vectors.
