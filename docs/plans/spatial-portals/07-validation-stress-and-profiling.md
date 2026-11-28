# Validation, Stress, and Profiling

Status: proposed execution specification, revision 2 (2026-11-27). Owns Stage 0 and
Stage 14, and the verification every other stage runs.

Happy-path screenshots are not evidence. The aim is to falsify these claims:

- one mapping is shared by every consumer;
- relocation carries or resets every piece of spatial state;
- authority, owner and replay agree on a crossing;
- nothing is ever drawn between two apertures;
- nothing behind an exit plane is ever drawn;
- a coupled body behaves as one mass;
- work follows visible linked content and active crossings, not total content;
- the zero-link path is unchanged.

## 1. Verification discipline

### 1.1 Every stage

```sh
cmake --preset dev
cmake --build --preset dev --parallel
ctest --preset dev
git diff --check
```

Focused tests first while iterating. CTest runs serially. New test names are confirmed
with `ctest --preset dev -N -R '<pattern>'` before they are reported as passing.

Additional runs by kind of change:

| Change | Also run |
|---|---|
| Rendering | golden images; `scripts/bench_render_ab.sh` against the pre-change build |
| Particles | `scripts/bench_particles.sh` |
| Physics | `physics_isolation`; the `asan` preset |
| Concurrency | `worker_count == 0` and the parallel path; the `tsan` preset |
| Module boundary, components | ABI, layout, component roster and codegen checks |
| Cook | repeated cook for identical bytes; old and new fixtures |
| Editor | undo, redo, cancel, focus change, document close; editor layering and mesh-edit fitness tests |
| Vocabulary | `scripts/check_declared_vocabulary.sh`; the vocabulary reference test |
| Replication | the hosted authority plus client run |

A command that was not run is reported as not run.

### 1.2 Regression standard

A regression test fails before the fix for the intended reason. No tolerance widened to
pass, no sleeps, no replaced snapshots, no skipped lifecycle states, no averaging away an
outlier. A benchmark that dropped work is a failed run.

### 1.3 Start of each stage

Reread the facts the stage depends on in `00`, section 3, against the tree. Reread
`docs/deferred.md` for triggers the stage hits. The "verify at stage start" items in
each document are part of the stage.

## 2. Evidence

`docs/plans/evidence/spatial-portals/<topic>/`, one directory per measured question,
each with a `README.md` giving the question, commits, hardware, build, scene, method,
commands, warmup, sample count, result and verdict, plus reduced JSON or CSV. The
acceptance rubric is written before the result. Large captures are not committed unless
they preserve a failure.

## 3. Fixtures

Test content must be cooked by a fixture; `.cooked/` is not in source control. Fixture
names include something unique per worktree so concurrent runs do not collide.

| Fixture | Contents |
|---|---|
| Link world | Same-zone and cross-zone links; demand-only link; one-endpoint link; doubly claimed aperture; mismatched extents; overlapping zones; a shallow recess; a non-upright link; zone unload and reload controls |
| Character world | Persistent pawn and a zone-owned character; links at 0, 90 and 180 degrees of yaw; a floor-to-wall link that must refuse; thin and thick rims; a ledge just past an exit; high-speed launch; participation controls; fixed input replay |
| View world | Two rooms with asymmetric landmarks and different lighting; geometry touching and crossing the exit plane; a link seen through a link; scripted camera path; a moving marker; no randomness |
| Replication world | The character world under the hosted authority plus client harness, with controllable latency and a forced one-tick misprediction |
| Presentation world | Orbit camera rail; a burst and a steady emitter aimed through an aperture; a trace and a projectile range; a navigating agent with a route through a same-zone link |
| Body world | Sphere, box, capsule, convex, compound, pole, plate; floors and walls each side; stacks; launchers; fixed tick count; pose, velocity, contact and handle traces |
| Lighting world | Source-only point and spot lights; a blocker on each side; a narrow aperture; baked surfaces; probe volumes each side; a moving shadowed light |
| Scale world | Many links, apertures, lights and entities that are not relevant to the view or to any nearby body |

## 4. Adversarial matrices

### 4.1 Link lifecycle

World scene before zones and after; A before B and after; unload and reload of either
or both; link deleted and recreated; aperture moved between zones; reference to an
entity that no longer exists; level change with a link resolved; shutdown with links
resolved and awaiting.

Assert: no runtime id persisted; ordered output stable; awaiting distinct from invalid;
one diagnostic per transition; an invalid link is inert.

### 4.2 Geometry

Orientation: facing pair, 90 and 180 degrees of yaw, wall to floor, floor to wall,
upside down, nearly parallel planes.

Viewpoint: far, grazing, one pixel, full screen, eye near the plane, eye at an edge and
a corner, camera behind the aperture, near plane cutting the rim, two visible links.

Aperture: minimum valid extent, very wide, very tall, exact edge and corner, zero and
negative extent, scale just inside and outside tolerance.

Motion: stationary overlap, slow, fast, reverse before the centre crosses, reverse
straight after clear, oscillation.

### 4.3 Character

1. Persistent pawn crosses with no partition move.
2. Zone-owned character crosses, keeps its id and mover, belongs to the far zone.
3. Stop with the centre short of the plane.
4. Stop with the centre exactly on the plane.
5. Back out after several ticks.
6. Slide along the rim; approach a corner.
7. Exit over empty space; exit onto a ledge.
8. Destination becomes ready while waiting.
9. Destination loses participation before commit.
10. Link invalidated while overlapping.
11. Zone torn down while overlapping.
12. Faster than the recess depth per tick.
13. Two apertures on one segment.
14. The same input at two render rates.
15. A time-dilated character.
16. A session-based movement mode (tether, cling, flight) active at the crossing.
17. Save while overlapping, load, continue.

Assert: finite state; deterministic choice; no rim tunnelling; no double crossing;
velocity and facing carried; support found again; serial advanced once; identical ticks
across render rates.

### 4.4 Replication

Remote sampled at every render time across a crossing; owner predicts correctly;
authority early by a tick; authority late by a tick; pre-crossing command executed
post-crossing; refused crossing predicted as accepted; replay across a crossing; packet
loss around the crossing tick; a crossing older than the mapping ring.

Assert: never drawn between apertures; pose and facing agree after correction; the
correction is bounded; the direction the player meant is the direction moved.

### 4.5 View

Every orientation and distance class; geometry through the exit plane; a link seen
through a link; more requests than the cap with reversed load order; resize each frame;
swapchain recreation with views live; zone leaving the Visible set after extraction;
hot reload while visible; a skinned mesh in both views; profiling off, counters, GPU
and capture.

Assert: no recursion; nothing from behind the exit plane; no stale target; deterministic
cap result; no target leak; only the far zone and the persistent partition visited.

### 4.6 Particles

Burst through; steady rate through; particle crossing back; more linked states than the
cap; emitter straddling the slab but no particle crossing; a one-way link; an invalid
link mid-effect; hot reload of an effect with a linked state; level change with linked
states live.

Assert: exact landing; block invariants on every chain; refusals counted; nothing held
is disturbed; the pool returns to its starting free count.

### 4.7 Bodies

Shapes and contact modes from `04`. Lifecycle: wake near an aperture, sleep while
straddling, wake from far-side contact, destroy while coupled, collider removed, link
invalidated, zone torn down, shutdown with a proxy live. At least 10,000 ticks of
repeated traversal, gravity fall, rim contact and rest.

The acceptable envelope is set against no-link controls before link results are read.

### 4.8 Lighting

The cases in `05`.

## 5. Counters

A field is added with its producer, never ahead of it.

| Group | Counters |
|---|---|
| Links | active, awaiting, invalid, rebuilds |
| Traversal | overlap starts, commits, back-outs, refusals by reason, deferred second crossings, teardown resolutions, leases acquired and released |
| Relocation | relocations, refusals by unregistered component |
| Views | requested, drawn, dropped, target pixels and bytes, queue items, draw calls, composite draws, fallback draws |
| Replication | held interpolation samples, commands carried, commands older than the ring, facing corrections |
| Particles | crossings, linked states live, refused crossings, particles dropped from linked views |
| Bodies | candidates, uncoupled transfers, coupled bodies, proxies, contacts accepted and rejected per side, authority changes, coupling error, bodies refused by the cap |
| Lighting | as `05`, section 5 |

GPU scopes: `LinkedViews`. A transmitted-shadow scope only if the aggregate shadow scope
cannot answer a measured question. No per-link timestamps.

## 6. Benchmarks

### 6.1 Stage 0 baselines

Before any link code, on the scale world with links absent: frame CPU and GPU by scope,
extraction, the fixed tick, particle simulate and extract, cook time per step, memory.
These are what "inside noise" is measured against.

### 6.2 Render sweep

Release build, scripted camera, immediate present, fixed resolution, warmup discarded,
enough frames for median, p95 and p99.

- visible links: 0, 1, 2, cap plus one;
- target scale: 0.25, 0.5, 1.0;
- projected size: tiny, quarter screen, full screen;
- far-side content: low, medium, stress;
- straddling renderables: 0, 1, 8, 32 (from Stage 8);
- transmitted lights: 0, 1, 8 (from Stage 13);
- unrelated resident zones: 0, 8, 32.

### 6.3 Tick sweep

- active links: 0, 1, 8, 32, with a constant high count of unrelated ones;
- characters near an aperture: 0, 1, 16, 64;
- bodies near an aperture: 0, 1, 16, 64;
- coupled bodies: 0, 1, 8, 32 (from Stage 11).

### 6.4 Particle sweep

The existing `bench_particles.sh` scenarios with links absent, then with one aperture in
the path of a dense emitter at 10k and 100k particles.

### 6.5 Cook sweep

Links 0, 1, 8, 32; lights reaching apertures 0, 8, 32, 128; serial and worker counts.
Record time per step, BVH tests, peak memory, output hashes, unchanged-zone cache hits.

### 6.6 Lifecycle sweep

Repeatedly: attach and detach aperture zones; resolve and unresolve links; cross and
return; recreate targets; create and destroy proxies; shut down. Record retained memory
and resource counts returning to their start.

## 7. Scaling laws

Numbers come from measurement. These shapes are binding.

- **Zero links.** No resolution beyond one empty query, no crossing work beyond one
  empty-span check, no linked view, target or scratch block, no particle crossing test,
  no proxy table work, no cook work. CPU, GPU and allocations inside noise.
- **Resolution.** Proportional to link and residency changes. Never a world scan per
  frame.
- **Traversal.** Proportional to characters and bodies near active apertures.
- **Views.** Proportional to drawn target pixels and visible far-side content.
- **Particles.** One bounds test per emitter per nearby aperture; per-particle work
  only for emitters that pass.
- **Replication.** Constant per sample and per command.
- **Lighting.** As `05`, section 6.
- **Memory.** Bounded by the stated caps. Every retained resource has a teardown test.

## 8. Limits introduced by this suite

Each has a default, an overload behaviour, a counter and a boundary test, and is
recorded where shown. A limit without all four is a defect however generous the number.

| Limit | Stage | Recorded in |
|---|---|---|
| Linked views per frame | 4 | `docs/renderer/constraints.md` |
| Linked view target scale and bytes | 4 | `docs/renderer/constraints.md` |
| Main view-uniform scratch floor | 4 | `docs/renderer/constraints.md` |
| Relocation mapping ring per pawn | 5 | `docs/plans/networking.md` or its successor document |
| Linked emitter states per effect instance | 6 | `docs/particles/runtime.md` |
| Coupled bodies and proxies | 11 | `docs/physics/` |
| Transmitted light images per view, shadowed images per view | 13 | `docs/renderer/constraints.md` |

## 9. Documentation that changes with the code

- `docs/deferred.md`: entries from `00`, section 9, added by the stage that creates
  them; "Presentation consumers observing a teleport" removed by Stage 2.
- `docs/plans/engine-roadmap.md`: the world-partition status line and Track C item 7
  when v0.1 lands; the relocation operation against Track C item 6.
- `docs/renderer/`: `frame.md` and `features-and-passes.md` for runtime composition;
  `constraints.md` for limits and the corrected push-constant size; `extending.md`,
  which still describes registration order as draw order.
- `docs/gameplay/`: `camera.md`, `saves.md`, `relocation.md`.
- `docs/particles/runtime.md`, `docs/navigation/runtime.md`, `docs/physics/`.
- `editor/ARCHITECTURE.md` for the entity-reference property kind.
- A subsystem document for links themselves, written when Stage 1 lands. This suite is
  a plan; that document is the reference.

## 10. Completion checklist

Before v0.1, v0.2 or v1.0 is called complete:

- [ ] `CLAUDE.md` reread against the final diff.
- [ ] One mapping, read from `ResolvedLinks`, by every consumer.
- [ ] One relocation operation; every discontinuous move calls it.
- [ ] No space id, dimension or second world.
- [ ] No runtime id or partition id persisted.
- [ ] No engine identifier named for a genre or a game.
- [ ] Vulkan and Jolt behind their boundaries; the backend never queries ECS.
- [ ] Authority, owner and replay agree on every crossing in the matrix.
- [ ] Every limit has a default, an overload behaviour, a counter and a boundary test.
- [ ] Zero, one and many links pass.
- [ ] Load, unload, reload, level change and shutdown pass.
- [ ] Serial and worker paths match where a worker path exists.
- [ ] `asan` passes the lifecycle; `tsan` if concurrency changed.
- [ ] `ctest --preset dev` passes serially; `git diff --check` passes.
- [ ] Baseline and feature measurements recorded with median, p95 and p99.
- [ ] The zero-link path is inside noise.
- [ ] The rejected coupling candidate is deleted (v1.0).
- [ ] Deferred entries are in `docs/deferred.md` with triggers.
- [ ] Nothing partial is called finished.
