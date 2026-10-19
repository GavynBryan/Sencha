# Spatial trigger programs

This runtime is the next unit after sensor observations. Kyusu brush creation,
sensor cooking, inspector stacks, viewport direction editing and the authored
Loss Function demonstration remain subsequent units in
[the implementation plan](../plans/data-driven-triggers.md).

## Ownership and execution

`Trigger` contains activation settings and a generational handle, with no owning
strings or arrays. `ComponentTraits<Trigger>` attaches and detaches programs in
the World's `TriggerPrograms`. The store's owner index makes repeated component
initialization safe: package import preallocates uninitialized ECS columns, so
initialization cannot read a prior handle from those bytes. A same-World entity
copy receives an independent authored program. Cross-World transfer goes through
scene serialization. Destruction, removal and import rollback release ownership.

The authored program remains a value tree, preserving unknown command identifiers
and arguments without a game module. Its compiled representation is immutable;
an activation retains it while dispatching, so a command may destroy its producer
without invalidating the current call.

Before consuming an occurrence batch, the fixed-logic system prepares newly
attached or edited programs. A clean store checks dependency stamps in constant
time. Changed sources compile only their own entries; catalog, tag, component
vocabulary and asset metadata changes refresh affected bindings. First contact
therefore does not parse authoring data or resolve names. `Resolve` also checks
stamps for native consumers; ordinary scheduled activations hit prepared entries.

`CompileAuthoredArguments` supplies schema-to-slot compilation for both Verb and
Query calls. `ResolveAuthoredArguments` supplies the same dynamic input validation,
persistent entity resolution and data-subtype checks both use. Commands invoke the
existing `VerbDispatcher`; conditions call `AuthoredQueryDispatcher::Evaluate`.
There is no trigger command registry, expression evaluator or gameplay dispatch
from a physics callback.

Physics publishes sample N after bodies and CharacterVirtual finish moving.
`TriggerSystem` consumes it once, at authoritative fixed logic N+1, before
`MotionCompositionSystem`. Current/future samples cannot execute early. Client
samples are discarded and cannot replay after an authority change. No new RPC or
client prediction path is added.

## Program representation

A version-1 inline program uses ordinary binding wrappers:

```json
{
  "version": 1,
  "require_components": ["sencha.physics.character_controller"],
  "conditions": [],
  "commands": [{
    "enabled": true,
    "verb": "movement.apply_velocity",
    "arguments": {
      "target": {"input": "activator"},
      "direction": {"input": "trigger_up"},
      "speed": {"const": 17},
      "operation": {"const": "replace"}
    }
  }]
}
```

The movement example requires PR #177. This is explanatory data, not a claim that
the authored bumper level or Kyusu workflow is already delivered.

Component eligibility is an AND-list of stable component identities compiled to
dense component IDs. Conditions contain a Query name and argument map; all must
answer true. False short-circuits; unavailable, unbound or stale answers fail
closed. Disabled condition/command rows survive serialization and do not compile.
Any invalid active row invalidates the whole program. Commands run in order and
stop on the first refusal; earlier accepted operations are not rolled back.
Leases need an explicit owner/lifetime and cannot be used as instant commands.

| Binding input | Runtime value |
| --- | --- |
| `trigger` | This Trigger's complete entity identity |
| `activator` | The activator's complete entity identity |
| `event` | Enum choice Enter, Stay or Exit |
| `tick` | Observation tick; invocation provenance carries the execution tick |
| `trigger_up` | Trigger world rotation applied to local +Y |
| `trigger_forward` | Trigger world rotation applied to local -Z |
| `trigger_right` | Trigger world rotation applied to local +X |

Axes ignore scale. Constant world vectors use the existing `const` wrapper.
Custom local vectors and interactive direction affordances remain part of the
forthcoming direction-authoring unit.

## Activation policy

- Enter runs once per occupancy episode. An observed Stay with no retained
  history is treated as initial Enter, including enabling a trigger while occupied.
- Stay is eligible immediately, then at most once per completed physics sample
  and no faster than `StayInterval` world simulation seconds. It never executes a
  catch-up loop of missed activations.
- Exit runs on normal departure. Transient crossings can publish Enter followed
  by Exit in the same sample. Teardown never creates synthetic gameplay Exit.
- Cooldown is a minimum interval in world simulation seconds, globally or per
  activator. Events during cooldown are dropped. Stay retries at its next cadence.
- Once retains history across ordinary departure for the selected scope.
  Disable, removal, destruction or losing zone logic/physics participation resets
  that residency/enable episode. Returning or re-enabling samples occupancy anew.
- Policy is consumed when conditions and budgets admit an activation, before its
  commands execute. A command refusal does not refund cooldown or once history.
  Validation, condition or budget failures do not consume policy.

## Bounds and diagnostics

Programs permit at most 64 command rows, 32 Query conditions and 32 required
components. Retained policy/occupancy states default to 4,096. Global policy uses
an additional state per trigger; once-per-activator history is retained across
normal departure until reset. Overflow refuses new states without evicting valid
cooldowns or once history.

`logic.trigger.pair_capacity` controls admission capacity; reducing it preserves
existing retained history. `logic.trigger.command_budget` defaults to 4,096
command attempts per tick. An activation exceeding the remaining budget is
refused whole, before dispatch. The copied observation batch is bounded at twice
the pair limit. An oversized sample executes nothing; endpoint occupancy can
release stale state without synthesizing an Exit command. Sensor-side observation
overflow retains the physics layer's last complete occupancy contract.

Compilation errors name the full trigger identity, row and argument and are
logged once per rebuild. Runtime attempts use existing Verb traces, with Trigger
as producer and activator as instigator. `Stats()` exposes executed commands,
gated activations, failures and overflow. Existing Verb reentrancy and buffered
Event budgets remain authoritative for chains.

For P retained states and O occurrences, warmed execution costs
O(P + O log P + typed argument/condition/command work). Initial insertion may
shift O(P) compact records in bounded reserved storage. Preparation scans entries
only after source/dependency changes, compiling only invalid entries. There is no
per-frame ECS scan of trigger entities. Scalar, entity, enum and vector inputs
allocate nothing after warmup; larger owned payloads retain the existing authored
value/dispatcher copy costs.

## Serialization and SDK compatibility

The component uses a new `trigger` scene key and `TRGR` binary chunk. Its inline
program has an explicit version; unknown active vocabulary remains inspectable
but must fail program validation/cook. No existing scene format version changes.
The normal scene serializer and `.smap` package import use the same component
serializer; runtime handles are never persisted.

The user approved the shared archive API extension on 2026-10-19.
`IWriteArchive::Value(key, const JsonValue&)` and
`IReadArchive::Value(key, JsonValue&)` preserve dynamic subtrees. Existing scalar
operations retain their encoding. JSON archives copy the DOM; legacy binary
archives use existing JSON stringify/parse and length-prefixed strings for this
new operation. `.smap` retains its existing interned value-tree codec.

Adding virtual archive operations changes the SDK ABI. Archive implementers must
implement them, and modules must rebuild. Archive headers are now explicit module
fingerprint inputs, so host/module skew fails at the existing boundary. The
trigger's component table also changes the same-build session contract through
the existing component manifest hash.

## Reproducible execution measurement

```sh
SENCHA_TRIGGER_BENCH_OUT=/tmp/trigger-execution.json \
  taskset -c 2 build/test/runtime_tests \
  --gtest_filter=TriggerFixture.ExecutionBenchmark
```

The gated test measures fixed-logic consumption with one Query and two Verb calls
per active trigger. It uses 128 warmup ticks and 1,000 measured ticks at 120 Hz for
0, 1, 64, 512 and 2,048 active triggers. `SENCHA_TRIGGER_BENCH_REPS` changes the
sample count. It checks invocation counts and warmed C++ allocations. Setup,
physics queries and sensor publication are excluded; these numbers must not be
presented as full physics-and-trigger costs. Sensor measurements are documented
separately in [the sensor review](../physics/sensor-overlap-review.md).

Measured on an Intel Core i7-13620H (16 logical CPUs), GCC 15.2.1, the
`profile` preset (`-O3`, `NDEBUG`, symbols and frame pointers), pinned to core 2.
The build and other tests were idle during measurement.
[Raw results](benchmarks/trigger-execution-profile.json) retain all samples.

| Active triggers | Median ms | p95 ms | Worst ms | Warmed allocations |
| ---: | ---: | ---: | ---: | ---: |
| 0 | 0.000135 | 0.000138 | 0.000146 | 0 |
| 1 | 0.000593 | 0.000607 | 0.005041 | 0 |
| 64 | 0.032077 | 0.033877 | 0.050855 | 0 |
| 512 | 0.267701 | 0.276920 | 0.299894 | 0 |
| 2,048 | 1.152710 | 1.256710 | 1.854010 | 0 |

At 120 Hz, 512 activations consume about 3.2% of the fixed-tick budget at the
median; 2,048 consume 13.8%. At 60 Hz those shares halve. Physics observation
cost must be added; this is not evidence that dense combined scenes fit either
budget. These are feature costs, with no claimed speedup over a prior runtime.

Verification on this source revision: the full dev build passed with warnings
as errors. Serial CTest reported zero failures across 6,148 enabled entries
(17 opt-in generators skipped; one additional test disabled), taking 334.73 s.
The optimized runtime's focused trigger tests passed (35 cases; the benchmark
is opt-in) and its benchmark was executed separately. Module ABI, layout,
physics isolation and editor dependency checks are included in the full suite.
