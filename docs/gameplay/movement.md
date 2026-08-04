# Character Movement

Status: **current architecture** (2026-08). This documents the movement runtime
as it exists in the tree: `engine/include/movement/`, `engine/src/movement/`,
the character motor in `engine/include/physics/CharacterMover.h`, and the
authored profile format. Tests live in
`test/framework/MovementArchitectureTests.cpp`,
`test/framework/MovementProfileDataTests.cpp`, and
`test/editor/MovementResponseSimTests.cpp`.

Movement is a peer feature directory alongside `gameplay_tags/`, `attributes/`,
`effects/`, `abilities/`, and `camera/` — one system per file, registered
through its own `RegisterMovement*` functions. There is no movement manager and
no movement state machine class.

## The model

The pipeline is built on a strict three-way split, and most extension questions
answer themselves once the split is clear:

- **Physical facts** — what the world did to the character last step.
  `SupportState`, `Immersion`, `KinematicState`. Written by physics, read by
  everything. A fact is never a mode: nothing switches to "airborne" because
  support vanished; systems and authored data *condition on the fact directly*.
- **Locomotion decisions** — what the character wants this tick.
  `MovementIntent` (from input or AI), the active locomotion mode, and the
  resolved tuning coefficients. Exactly one mode system writes
  `LocomotionOutput` per character per tick.
- **The motor request** — the single composed `MotionRequest` the character
  motor consumes. Action producers (jump, dash, knockback) contribute through
  override/impulse channels; `MotionCompositionSystem` folds everything into
  one request, and only the motor reads it.

Behavior variation enters through data: tuning is authored in hot-reloadable
`movement.profile` assets, discrete actions are AbilityKit abilities and
effects, and locomotion state is exposed as gameplay tags. New modes enter
through a registry, not through engine edits.

## The fixed tick

All movement systems run in the `FixedLogic` phase; the motor runs in the
`Physics` phase of the same tick. Registration and ordering live in
`engine/src/movement/MovementRegistration.cpp`.

| # | System | Does |
|---|--------|------|
| 1 | `SupportTagProjectionSystem` | Mirrors last step's `SupportState` onto the `movement.grounded` tag. |
| 2 | `AbilityActivationSystem` (AbilityKit) | Activates abilities; e.g. jump's effect grants the one-tick `movement.jump.requested` tag. |
| 3 | `ModeRequestCollectionSystem` | Consumes granted `movement.request.*` tags into the arbitrated transition mailbox. |
| 4 | `LocomotionModeTransitionSystem` | Applies at most one mode transition per entity: exit old session, enter new, move the active tag. |
| 5 | `AttributeResolveSystem`, then `MovementTuningResolutionSystem` | Resolves this tick's `ResolvedMovementTuning` from the profile layers against the current facts, mode, and tags. |
| 6 | `FreeLocomotionSystem` (and any mode-owned locomotion system) | Writes `LocomotionOutput` for characters in its mode. |
| 7 | `JumpExecutionSystem` (and any action producer) | Contributes to the override/impulse channels. |
| 8 | `MotionCompositionSystem` | Composes the single `MotionRequest`; clears the channel mailboxes. |
| 9 | `CharacterControllerSystem` → `CharacterMoverPool::Drive` (Physics phase) | Collide-and-slide against the request; writes back `LocalTransform`, achieved `KinematicState`, and the new `SupportState`. |

Facts therefore describe the *previous* step: locomotion at tick N reads the
support and velocity the motor produced at tick N-1. That one-tick latency is
by design and is invisible at fixed rates; do not try to shortcut it by reading
the physics backend mid-tick.

## Components and resources

Components (all POD, `engine/include/movement/MovementComponents.h`):

| Component | Written by | Read by | Notes |
|-----------|-----------|---------|-------|
| `SupportState` | `CharacterMoverPool::Drive` | locomotion, tuning resolution, gameplay | `Stable` / `Steep` / `None`, contact normal, surface entity, surface velocity. **`Steep` covers any touched-but-unsupported contact, including vertical walls** (Jolt `NotSupported` maps to it), so a wall graze surfaces the wall normal here. |
| `Immersion` | volume systems | tuning resolution | Fraction of the capsule inside a volume. A fact, not a "swimming mode". |
| `KinematicState` | `CharacterMoverPool::Drive` | locomotion | Full achieved world velocity, vertical channel included. A character that hit a wall does not keep the velocity it asked for. |
| `MovementIntent` | input mapping / AI | locomotion | World-space planar wish direction, magnitude = input strength. Discrete actions are *not* intent flags; they are ability activations. |
| `CharacterMovement` | transition system (Mode), spawn (Profile) | everything | The authored profile handle plus the current `LocomotionModeId`. |
| `ResolvedMovementTuning` | `MovementTuningResolutionSystem` only | locomotion, action producers | This tick's coefficients. Nothing else writes it. |
| `LocomotionOutput` | the active mode's locomotion system | composition | Velocity plus up axis plus gravity scale. Exactly one writer per entity per tick. |
| `MotionAxisOverride` | action producers via `MotionComposition.h` helpers | composition | One-tick mailbox; planar/up channels, first-write-wins, forced variants replace. |
| `MotionImpulse` | `AddMotionImpulse` | composition | One-tick additive velocity (knockback, explosions). |
| `MotionRequest` | `MotionCompositionSystem` only | `CharacterMoverPool` | The single composed motor request. |
| `ModeTransitionRequest` | `RequestLocomotionMode` | transition system | The arbitrated transition mailbox. |
| mode session components (`ClingSession`, …) | mode registry hooks | that mode's systems | Added on entry, removed on exit; see Modes. |

World resources: `LocomotionModeRegistry` (the open mode set), `MovementTags`
(resolved `movement.*` tag ids), `MovementDefs` (the `MoveSpeed` attribute and
`Jump` ability ids), `MovementProfileBindingCache` (bound profiles, created on
demand by the tuning system).

## Modes

A locomotion mode is a **registration, not a type**
(`engine/include/movement/LocomotionMode.h`). Registering mints two gameplay
tags — `movement.mode.<name>` (active) and `movement.request.<name>` — and
optionally binds a session component whose add/remove the registry owns. A game
module adds a mode without touching any engine system; `Free` is the only
built-in.

```cpp
LocomotionModeRegistry& modes = world.GetResource<LocomotionModeRegistry>();

// Stateless-entry mode with session state:
const LocomotionModeId flight = modes.Register<FlightSession>("movement.mode.flight");

// Mode that must capture a surface before entering:
const LocomotionModeId cling = modes.RegisterWithCandidate<ClingSession, ClingCandidate>(
    "movement.mode.cling",
    [](const ClingCandidate& c) { return ClingSession{ c.Surface, c.Normal, c.Anchor }; });
```

Mode ids are registration order, stable within a run and meaningless across
runs. **Never serialize a `LocomotionModeId` or a mode's tag ids.** Authored
data references modes by name; the profile binding resolves names at runtime.

### Requesting a transition

Two producer paths feed one mailbox:

- Grant the mode's `movement.request.<name>` tag (typically from an ability's
  effect). `ModeRequestCollectionSystem` consumes the tag and files an
  `Explicit` request.
- Call `RequestLocomotionMode(world, entity, target, class)` directly from a
  system, with any `ModeRequestClass`.

The mailbox holds one pending request; `Automatic` loses to `Explicit` loses to
`Forced`, and equal class is first-write-wins. Use `Automatic` for
detection-driven entries ("wall is available"), `Explicit` for player commands,
`Forced` for scripted or damage-driven overrides.

### Applying a transition

`LocomotionModeTransitionSystem` applies at most one transition per entity per
tick. It collects requests first and applies them after iteration, because
entry/exit hooks add and remove session components — a structural change that
must not happen inside an active query. The sequence for an accepted request:
exit the old mode's session, enter the new one (converting the staged candidate
if the mode has one), set `CharacterMovement::Mode`, revoke every other mode's
active tag, grant the target's.

`CanEnter` runs **before** the current session is touched, so a candidate-based
mode can validate its staged surface without disturbing the live state. A
rejected or self-targeting request discards the staged candidate rather than
leaking it.

### The candidate pattern

For modes that must find something in the world before entry (a ledge, a wall,
a rail), the shape is:

1. A detection system stages a `Candidate` component (a POD holding the
   captured surface/normal/anchor) and files an `Automatic` request.
2. `CanEnter` is the registry-generated "candidate exists" check.
3. On acceptance the registry converts candidate → session atomically and
   removes the candidate; on rejection it discards the candidate.

`test/framework/MovementArchitectureTests.cpp` (`CandidateBecomesSessionAtomically`,
`MissingCandidateLeavesCurrentModeIntact`) is the executable specification.

### Mode-owned locomotion

Each mode owns one locomotion system that writes `LocomotionOutput` for
entities currently in that mode and skips everyone else (compare
`CharacterMovement::Mode`, or query `With<Session>`).
`FreeLocomotionSystem` is the reference: support-relative velocity, friction /
accelerate / drag / gravity through the pure kernels in `MovementStep.h`, all
coefficients from `ResolvedMovementTuning`.

`LocomotionOutput::UpAxis` is the seam that makes non-walking modes cheap: the
motor treats "planar" as the plane perpendicular to whatever up axis the output
declares, so a climbing mode can set the up axis along the wall normal and
reuse the same motor, step logic, and composition unchanged.

## Tuning profiles

Character coefficients are authored in `movement.profile` data assets
(`type: "movement.profile"`, version 1 — see
`template/assets/data/player_movement.sdata` for a live example) and resolved
every tick against the current facts. The asset compiles through
`MovementProfileData.cpp`, binds names → runtime ids through
`BindMovementProfile`, and is cached per handle by
`MovementProfileBindingCache`, which rebinds automatically on asset hot-reload
and on mode-registry growth.

```json
{
  "type": "movement.profile",
  "version": 1,
  "data": {
    "name": "player_movement",
    "layers": [
      { "name": "Base",
        "set": { "max_speed": 4.5, "acceleration": 10, "friction": 6,
                 "stop_speed": 1, "jump_speed": 6.9, "gravity_scale": 2.07 } },
      { "name": "Air control",
        "when": { "support": "none" },
        "set": { "friction": 0 } }
    ],
    "modes": [
      { "mode": "movement.mode.cling",
        "sustain": { "all": ["some.required.tag"] },
        "layers": [ { "set": { "gravity_scale": 0 } } ] }
    ]
  }
}
```

Resolution order (`ResolveMovementTuning`):

1. Start from `ResolvedMovementTuning` defaults, with `MaxSpeed` seeded from
   the character's `MoveSpeed` **attribute** — which is why sprint, slow, and
   haste are effects that modify the attribute, never branches in locomotion.
2. Apply the top-level `layers` in authored order. A layer applies only when
   its whole `when` condition matches: optional `mode` (by name), `support`
   (`any` / `none` / `stable` / `steep`), `immersion_at_least`, and a tag query
   (`all` / `any` / `none` against the character's tags).
3. Apply the active mode's `layers` from the matching `modes` entry, if any.

Within a layer each coefficient applies `set`, then `scale`, then `add`.
Authored order is the conflict-resolution rule — later layers win. Neutral
values (`set`/`add` 0, `scale` 1) mean an authored-but-untouched coefficient
never changes movement on its own.

The coefficient list exists in exactly one place: the `MovementTuningFields()`
table in `MovementProfileRuntime.cpp`. Layer application, the authoring schema,
and diagnostics all walk it.

`ResolveMovementTuning(..., collectTrace = true)` returns a per-layer trace
(matched, failure reason, coefficients after) — this is what the Data Editor's
rulebook view renders live, and it is the first tool to reach for when a
character feels wrong.

## Actions and composition

Discrete movement actions do not own the character; they contribute channels
that compose over the active mode's output
(`engine/include/movement/MotionComposition.h`):

- `TrySetPlanarMotionOverride` / `TrySetUpMotionOverride` — first-write-wins
  per channel. An action that arrives second this tick simply loses.
- `ForceSetPlanarMotionOverride` / `ForceSetUpMotionOverride` — deliberately
  replace whatever claimed the channel (reactions, scripted takeovers).
- `AddMotionImpulse` — additive velocity, applied after the channels
  (knockback, explosions).

Channels are support-relative: overrides replace the *controllable* planar/up
velocity, and the supporting surface's velocity is added afterward, so a jump
from a rising platform inherits the platform. The mailboxes are cleared by
`MotionCompositionSystem` after composing; a producer scheduled after
composition is a bug, and its value deliberately survives into the next tick
rather than being silently dropped.

Jump is the canonical action and the pattern to copy
(`MovementRegistration.cpp`): an AbilityKit ability gated on
`movement.grounded` and blocked by its cooldown tag; activation applies a
0.05 s effect granting the one-tick `movement.jump.requested` tag;
`JumpExecutionSystem` consumes the tag and writes `JumpSpeed` to the up channel
with a `Try` write (a jump loses to an action that already claimed the
channel). Even the jump's feel is partly authored — the template profile has a
"Jump startup" layer conditioned on the request tag. No system hardcodes "is
the player jumping".

## The character motor

`CharacterMover` (`engine/include/physics/CharacterMover.h`, Jolt
`CharacterVirtual` behind the physics PIMPL firewall) is deliberately dumb: it
collides, slides, applies step and ground-snap policy, and reports what it
achieved. It owns no velocity and no gravity — the movement pipeline composes
those — so the same motor serves any up axis and any mode.
`CharacterMoverPool::Drive` feeds it each character's `MotionRequest` and
writes back position, achieved velocity, and support.

Config knobs (`CharacterMoverConfig`): capsule radius/height, slope limit,
step height, ground-snap distance, skin width, push mass.

## Extension recipes

### Adding a locomotion mode

No engine edits required. In the game module:

1. **Session component** — a trivially-copyable POD holding what the mode must
   remember (surface, normal, phase, timer). Register it before entities exist.
2. **Register the mode** with the `LocomotionModeRegistry` (plain `Register`,
   or `RegisterWithCandidate` when entry depends on a captured surface).
3. **Entry** — a detection system that stages the candidate and files an
   `Automatic` request, an ability whose effect grants
   `movement.request.<name>`, or a direct `RequestLocomotionMode` call.
4. **Locomotion system** — one file, one system; writes `LocomotionOutput` for
   entities in the mode. Schedule it after `MovementTuningResolutionSystem` and
   before `MotionCompositionSystem`:
   `schedule.After<MyModeSystem, MovementTuningResolutionSystem>();`
   `schedule.After<MotionCompositionSystem, MyModeSystem>();`
5. **Exit** — the mode's own system requests `Free` (`Automatic`) when its
   conditions fail. Sustain tag queries can be authored in the profile today,
   but see the open items below before relying on them.
6. **Tuning** — add a `modes` entry to the profile; per-mode gravity, speeds,
   and caps are data, not constants in the mode system.
7. **Tests** — every movement system exposes a whole-world `Step(World&)`
   overload precisely so mode logic tests need no schedule, no physics, no app.

### Adding a movement action

Prefer the jump shape: ability (gating, cost, cooldown as data) → effect
granting a one-tick request tag → a small execution system consuming the tag
and writing a channel. Use `Try` writes unless the action is designed to
preempt; use impulses for additive shoves. Schedule the producer between the
locomotion systems and `MotionCompositionSystem`.

### Adding a tuning coefficient

Four places, all mechanical:

1. Fields on `ResolvedMovementTuning` (`MovementComponents.h`) and
   `MovementTuningPatch` (`MovementProfileData.h`).
2. A `MovementTuningFields()` entry (`MovementProfileRuntime.cpp`) — this wires
   layer application and diagnostics.
3. The authoring surface in `MovementProfileData.cpp`: a `FloatField` child in
   `TuningPatchSchema` and a line in `ParsePatch`.
4. The consumer that reads the resolved value.

Patch fields are optionals, so existing assets that never author the new key
are unaffected. No version bump for adding an optional coefficient.

### Detection systems and spatial queries

Ledge, wall, and vault detection belong in dedicated systems that read facts
and stage candidates — never inside a locomotion system's inner loop.
`PhysicsQueries` (`engine/include/physics/PhysicsQueries.h`) is the read-only
raycast/sweep/overlap surface; reach it from a game module via
`ctx.Schedule.Get<PhysicsStepSystem>()->GetSimulation()` (the template already
uses `Schedule.Get` for the shape cache). A detection system running in
`FixedLogic` queries last tick's physics state; stage the candidate and let the
next transition tick convert it. Keep detection work proportional to
characters that could actually enter the mode (gate on airborne, on speed, on
an ability tag) rather than sweeping for every character every tick.

### Adding a physical fact

Follow `SupportState`: physics (or a volume system) writes a POD fact
component, tuning layers and mode systems condition on it, and it is projected
onto a gameplay tag only if gameplay queries genuinely need tag-level access
(`SupportTagProjectionSystem` is the pattern). Do not invent a mode to carry a
fact.

## Rules and traps

- One writer per invariant: only the tuning system writes
  `ResolvedMovementTuning`, only the active mode writes `LocomotionOutput`,
  only composition writes `MotionRequest`.
- Mode ids and tag ids are registration-order runtime values. Serialize names,
  never ids.
- `Steep` support is not just slopes — a wall graze while airborne reports
  `Steep` with the wall's normal. Condition carefully (`support: "steep"`
  matches both), and treat it as the cheap wall-contact signal it is.
- Session add/remove is structural and happens only inside the transition
  system's apply phase, via the registry hooks. Do not add or remove session
  components from inside a query.
- The override/impulse components are one-tick mailboxes; ordering against
  `MotionCompositionSystem` is part of an action's correctness, not a detail.
- Movement systems early-out when their components or resources are absent, so
  they are safe in editor and test worlds that never registered movement. Keep
  that property when adding systems.
- Profile hot-reload works through the binding cache; there is no manual
  invalidation to call. If tuning seems stale, check the asset actually
  recompiled (`MovementProfileBindingCache::RebuildCount`).

## Open items

- **Sustain queries are authored but not enforced.** The profile schema and
  `MovementModeSustainMatches` (`MovementProfileRuntime.h`) exist with resolver
  tests, but no runtime system evaluates sustain and requests an exit. Until
  that lands, every mode must exit itself in code.
- **`ModeRequestClass::Forced` does not currently bypass `CanEnter`.** The
  comment on `ModeRequestClass` says a forced request bypasses the target's
  entry check; `LocomotionModeTransitionSystem` evaluates `CanEnter`
  unconditionally, and no test pins the bypass. Treat forced as "wins
  arbitration" only until this is reconciled.
- **`CharacterMoveRequest::AllowGroundSnap` is not driven by the pipeline.**
  The mover supports disabling ground snap for deliberate ground exits, but
  `CharacterMoverPool::Drive` always leaves it on; jumps currently escape snap
  on velocity alone. A mode that needs a guaranteed clean ground exit (mantle,
  launch pads) will need this plumbed through `MotionRequest`.
- **`PhysicsQueries` has no gameplay consumer yet.** The surface is ready; the
  first detection system establishes the access-and-phase pattern described
  above.
- **`ClingSession` / `FlightSession` ship as engine components** with no
  registered runtime mode; they anchor the candidate/session mechanism and its
  tests. A real climbing or flight feature may reuse or replace them.
