# Unified runtime world source-review amendments

Status: binding amendments discovered while implementing the program. These
clarify the owning plan without changing its one-world, zone-partitioned
thesis.

## World-scoped resources reuse `World` resources

The ECS `World` already owns a typed resource table whose lifetime is explicitly
ordered after component `OnRemove` hooks. Existing component lifecycle contracts
use it for asset/runtime lookup state such as `StaticMeshComponentAssets`,
`AudioSourceRuntime`, gameplay tag registration, and attribute/effect support.

The unified runtime therefore does **not** add a second world-level
`ResourceRegistry` beside `World::AddResource`.

Final homes:

- **Simulation-scoped ECS resources:** `RuntimeWorld::Entities` resources.
  This includes `ActiveCameraService`, asset lookup state, lifecycle-hook
  dependencies, world-lifetime gameplay registries, and event channels whose
  semantic owner is the entity world.
- **Zone-scoped non-entity resources:** `RuntimeZoneRecord::Resources` using the
  existing `ResourceRegistry`, admitted only when lifetime is exactly one
  resident zone and the state is neither entity-indexed nor backend-retained.
- **Retained backend state:** engine-owned backend scenes and their dedicated
  record-family modules. Physics bodies, movers, constraints, voices, emitters,
  navigation agents, and future vehicle objects never live in either resource
  table.

This removes an otherwise redundant simulation-level resource container and
keeps component teardown able to reach every resource its hooks require.

## Active camera is simulation-scoped

`ActiveCameraService` currently lives once per registry only because runtime
entity identity is split across registries. It stores one active `EntityId` and
is consumed by camera follow and render extraction. Under one entity world it is
one `World` resource, not zone metadata.

## Component schema implementation decision

The concrete startup mechanism is `WorldComponentSchema`:

- engine and game code add concrete component types in deterministic order;
- the schema is sealed before `Game::OnStart`;
- applying it to independent Worlds produces identical dense `ComponentId`
  values;
- scene serializers are validated to have matching runtime storage before game
  startup;
- game-instantiated registration callbacks are cleared before the module may be
  unloaded.

The current measured engine prefix contains 24 component types. The template
adds two. The fixed 256-bit signature therefore retains substantial near-term
headroom; widening remains trigger-driven rather than speculative.

## Query-order implementation decision

Phase 2 preserves existing traversal order:

1. cached matching-archetype order,
2. flat chunk order within each archetype,
3. row order.

Partition filtering is one O(1) membership check per non-empty chunk. The
unfiltered path is a separate compile-time instantiation and pays no membership
branch. The plan's earlier zone-slot-major ordering is not adopted because it
would require regrouping or a second traversal structure without measured
benefit.
