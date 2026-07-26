# World component schema

Status: unified-runtime-world prerequisite.

A unified runtime `World` must know the complete engine-plus-game component
vocabulary before its first entity is created. Streamed zones cannot register
component storage independently: they may load in any order, workers cannot
mutate the live world, and registration order determines dense `ComponentId`
values used by archetype signatures.

`WorldComponentSchema` is the frozen registration recipe for one runtime game
configuration.

## Construction

Engine and game setup add concrete component types in deterministic order:

```cpp
WorldComponentSchema schema;
RegisterEngineRuntimeComponents(schema);
game.RegisterRuntimeComponents(schema);
schema.Seal();
```

Every runtime world for that game applies the sealed schema before creating an
entity:

```cpp
World world;
schema.Apply(world);
```

The schema stores:

- stable `ComponentTypeId`;
- stable diagnostic name;
- size, alignment, and tag status;
- a concrete registration operation for the component type.

It is not a serializer registry, service locator, factory hierarchy, or dynamic
plugin API. It is a concrete ordered recipe crossing the existing game-module
boundary during startup.

## Ordering and identity

Stable `ComponentTypeId` values identify component contracts across modules and
cooked data. Dense `ComponentId` values remain runtime indices assigned by the
sealed schema's order.

Applying one schema to independent worlds produces identical dense IDs. A world
may already contain the exact same registration prefix, which supports existing
editor and test setup during migration. A divergent prefix fails loudly.

Cooked zone packages carry stable `ComponentTypeId` values. Import resolves them
against the already-applied world schema. Unknown types fail before partial
publication; import never registers a component.

## Lifetime

The schema has two states:

- **building:** engine and game code may add component types;
- **sealed:** immutable and applicable to worlds.

Adding after seal, applying before seal, applying after entity creation, or
applying against a different registration order is a contract violation.

## Component budget

The current runtime signature remains a fixed 256-bit value. The schema enforces
that budget and exposes its current size and remaining capacity for startup
diagnostics.

This phase does not widen or replace the signature. Before the unified runtime
world becomes the default host, the engine and target game must report their
actual sealed schema size. The fixed signature remains only if that measurement
leaves comfortable production headroom; widening is performed before content
relies on the final runtime ABI.

## Relationship to existing manifests

`EngineSceneComponents` remains the authoritative list of engine components that
serialize into scene documents. It is intentionally narrower than the runtime
world schema, which must also include:

- runtime-only engine components and links;
- movement, camera, physics, abilities, and other framework components;
- game-module components;
- tags that may appear on persistent or streamed entities.

Serializer registration and world-storage registration share stable component
identities but remain separate responsibilities.

## Phase boundary

This mechanism does not yet change the `Game` ABI or runtime startup sequence.
The next stacked integration phase will assemble one schema from engine and game
registration hooks, apply it to the unified world before entity creation, and
add component-count diagnostics. Streamed loading remains unchanged until the
package-import phase.
