# Sensor overlap observations

`RegisterPhysics` installs `SensorOverlapSystem` after
`CharacterControllerSystem` in the physics phase. It publishes the completed
sample in the World's `SensorOverlaps` resource. It never invokes gameplay
callbacks. Fixed logic can consume that sample on the following tick, before
movement composition and physics. Consumers must check authority, entity
liveness and domain participation again before executing gameplay.
`ResponsesSystem` is the engine's consumer: it delivers `sensor.enter`,
`sensor.stay` and `sensor.exit` at each volume that has responses
(`docs/gameplay/responses.md`).

The physics scene retains the non-static, non-sensor body candidates. The
character mover pool retains `CharacterVirtual` candidates, which have no body
in the broadphase. Both query Jolt's narrow phase against the Trigger object
layer using their existing backend shapes. No primitive shape is rebuilt per
sample, and no sensor-by-character Cartesian scan is performed. Sensors must
be created with `IsTrigger` and the Trigger collision layer; the ECS binding
derives both from `Collider.IsTrigger`.

Each sample collects endpoint overlaps and linear shape casts from the last
sampled position. Endpoint occupancy wins over a cast hit. The resulting pairs
are deduplicated and ordered by complete sensor `EntityId`, then complete
activator `EntityId`, including generations. Occupied pairs produce Enter on
their first sample, then Stay on every subsequent physics sample. A departed
pair produces Exit. A cast through a volume with neither endpoint occupied
produces Enter followed by Exit in the same sample. These are physics facts;
gameplay consumers own stay cadence, eligibility and cooldown.

New bodies and characters sample initial occupancy. Character teleports and
reconciliation through `SetPosition` reset sweep history: skipped space is not
traversed, while the next sample reports destination occupancy. Static ECS
sensors follow their authored transform through the existing rigid-body
bridge. `PhysicsStepSystem` propagates simulation transforms before syncing
bodies, so current-tick local and parent edits reach the sensor pose.
Relocation therefore produces the endpoint Enter/Exit transitions of
the new pose. A moving sensor is not swept through stationary activators.

Entity destruction, removal of the sensor collider or activator backend link,
and departure from physics participation forget a pair without a gameplay
Exit. Zone-residency teardown also removes already published occurrences for
the departing partition while its entities are still available. A restored zone
starts with fresh Enter observations. A consumer holding its own pending work
must independently discard requests against departed or destroyed entities.

The sample is owner-thread only. Jolt's current simulation is single-threaded;
this mechanism adds no workers or contact-listener threading. Sorting removes
backend enumeration order from the published result. The casts use the final
orientation and the straight displacement between samples. They cover fast
linear crossings, including characters; they do not reconstruct rotating,
sliding, stair-stepping or multiple-substep trajectories. This remains a limit
of the observation implementation, not a continuous-path guarantee.

## Bounds and cost

The default capacity is 4,096 observations. Consecutive identical hits are
coalesced during collection; other duplicate subshape/cast hits count toward
capacity until the commit deduplicates them. `Capacity()` reports that
observation bound. Collection allocates no storage after construction. Pair
and occurrence buffers are reserved at construction, and the maximum published
transition count is bounded by three times capacity.

Overflow rejects the entire sample, publishes no transitions and preserves the
previous occupancy. `OverflowCount()` increments once for that rejected sample.
This avoids false Exit events from a partial query. Consumers must expose this
counter in their diagnostics; overflow can lose a transient crossing and is
never evidence that a volume was empty. A subsequent complete sample resumes
comparison with the last complete one.

For H admitted hits and P prior pairs, pair tracking costs
O(H log H + P), with O(capacity) retained storage. Sampling visits A retained
non-static body candidates and C character movers, including sleeping and
stationary ones. When sensors exist, spatial work is one overlap query per
candidate plus a cast when it moved, with broadphase enumeration and narrow-phase
costs owned by Jolt. Queries use the existing NonMoving broadphase bucket (all
Trigger-layer bodies map there) and the Trigger object-layer filter. Collector
`OnBody` copies identity while Jolt already holds its body lock, without a second
lock per hit. ECS liveness/domain validation costs O(H + P); admitted hit storage
and publications remain bounded. There is no second spatial index.

A scene-owned sensor count is maintained only when bodies are created or removed.
With zero sensors, body/character sweep origins still advance but no overlap or
cast queries are issued, and the static sensor-pose query is skipped. This is
O(A + C) pose sampling, not an O(1) whole-system promise. It avoids stale sweeps
when a sensor appears later, without an activation tracker. `QueryCounts()` on
`SensorOverlaps` reports actual overlap/cast calls for the sample, including an
overflowed sample; `Begin()` resets these counters. Sleeping bodies continue to
query: a sensor can relocate over them without moving or waking them.

The tracking microbenchmark is reproducible from the repository root:

```sh
c++ -std=c++23 -O2 -Iengine/include scripts/bench_sensor_overlaps.cpp engine/src/physics/SensorOverlaps.cpp -o /tmp/sencha_sensor_bench
/tmp/sencha_sensor_bench
```

On the implementation workstation, 200 reverse-order samples averaged about
1.5 microseconds for 100 pairs, 17 for 1,000 and 57 for 4,096. The initial
sorted-insertion implementation measured about 3.1, 88 and 1,865 microseconds
respectively. This measures tracking only, not physics queries or a game tick.
Allocation regressions cover the reserved tracker and warmed cached body/character
shape queries in `SensorOverlapAllocation`. Full-system measurements and review
dispositions are in [sensor-overlap-review.md](sensor-overlap-review.md).

## Movement and relocation contract

`PhysicsWorld::SetBodyTransform` retains its continuous-movement meaning. Its
existing kinematic binding caller still sweeps between samples, including fast
crossings. `TeleportBody` sets the pose and resets only that body's retained
sweep origin to the destination. It preserves linear/angular velocity and active
pair state, so occupancy compares the old sample with the destination and
publishes normal endpoint Enter/Stay/Exit. Physical movement after a teleport
still sweeps from the relocated pose. `DrivenPoseTarget::Teleported` uses that
same operation before the step integrates the supplied target velocity.

No distance threshold classifies a relocation. A caller that applies a
correction or teleport must use `TeleportBody`, rather than continuous
`SetBodyTransform`. For an ECS-driven kinematic body, the authored pose must
also agree with the destination before the next kinematic push. There is no
rigid-body network reconciliation path in this branch: `RigidBody` is not a
replicated component. Pawn reconciliation uses
`CharacterMoverPool::RestorePosition`; partition clamps and explicit character
teleports use `SetPosition`. Both reset character sweep history, while only the
latter requests a presentation snap. Correction displacement is never a cast;
subsequent replay/movement is still observed. These local observations do not
confer gameplay authority.

The last discontinuous relocation establishes the sweep origin for the next
sample. Earlier movements within that unsampled interval are not replayed;
intermediate destinations do not publish occupancy. This matches the existing
character endpoint contract. Moving sensors retain endpoint-only semantics.
Relocation resets history with a linear lookup of the retained body candidates;
ordinary sampling does not perform that lookup. `ObserveBodySensors` is private:
callers cannot inject an arbitrary origin that bypasses the relocation contract.

## Integration boundary and transform ownership

`PhysicsWorld` is the engine facade around the Jolt backend, not a standalone
Jolt library. It already carries entity identity in body user data and character
support results. Sensor queries unpack that same complete identity into plain
observations; they never visit ECS storage. `SensorOverlapSystem` owns ECS
liveness/domain validation and the World owns retained pair state. Gameplay sees
no Jolt types and physics executes no gameplay callbacks. An additional adapter
would move the same packing and collection work without removing a dependency
on ECS traversal, because no such traversal exists in the query code.

The pre-physics propagation in `PhysicsStepSystem` resolves current fixed-logic
local/parent writes before binding bodies. The existing post-physics propagation
in `EngineFramePhases` resolves poses written back by bodies and characters for
post-fixed consumers/history; presentation has its own domain. These are distinct
input/output boundaries, not interchangeable passes. Keeping the input pass at
the physics boundary also covers standalone schedules. Propagation is shared
engine machinery, not a sensor-specific hierarchy walk. Its query objects now
live in World resources and retain their archetype caches across sweeps; the
additional input pass does not add repeated query-construction allocations. A static child sensor
uses its parent's pose at that boundary; a parent's *subsequent* motion during
physics is visible at the next input boundary, not swept during this tick.

A collider's primitive sits at `CollisionShape::Center`, turned by
`Orientation`, in its body's frame; a sensor drawn off its entity's origin
keeps that entity's transform. Colliders persist in scenes (`collider`), which
is how a cooked trigger volume carries its sensor.

A static sensor follows the authored world pose. Kinematic bodies (including
sensors) follow the existing kinematic push contract. Dynamic bodies (including
sensors) own their simulated pose and write it back; authored pose writes must
not override them. Collider configuration is creation-time state, not a live
mutation API: editing `Collider.IsTrigger`, shape, mesh, or body motion does not
reconfigure a resident backend body. A fresh simulation binds the configuration supplied by its ECS
components. Generic runtime collider reconciliation is recorded in
`docs/deferred.md`; trigger-specific recreation would duplicate that owner.
