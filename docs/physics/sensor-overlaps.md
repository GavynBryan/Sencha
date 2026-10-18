# Sensor overlap observations

`RegisterPhysics` installs `SensorOverlapSystem` after
`CharacterControllerSystem` in the physics phase. It publishes the completed
sample in the World's `SensorOverlaps` resource. It never invokes gameplay
callbacks. Fixed logic can consume that sample on the following tick, before
movement composition and physics. Consumers must check authority, entity
liveness and domain participation again before executing gameplay.

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
O(H log H + P), with O(capacity) retained storage. Spatial work is one overlap
query per retained moving body or character plus a cast when it moved, with
candidate enumeration and narrow-phase costs owned by Jolt. Static world
geometry is not visited to enumerate activators.

The tracking microbenchmark is reproducible from the repository root:

```sh
c++ -std=c++23 -O2 -Iengine/include scripts/bench_sensor_overlaps.cpp engine/src/physics/SensorOverlaps.cpp -o /tmp/sencha_sensor_bench
/tmp/sencha_sensor_bench
```

On the implementation workstation, 200 reverse-order samples averaged about
1.5 microseconds for 100 pairs, 17 for 1,000 and 57 for 4,096. The initial
sorted-insertion implementation measured about 3.1, 88 and 1,865 microseconds
respectively. This measures tracking only, not physics queries or a game tick.
Allocation regressions cover the reserved tracker and warmed cached-character
shape queries in `SensorOverlapAllocation`.

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

