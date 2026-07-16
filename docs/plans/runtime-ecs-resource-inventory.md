# Runtime ECS Resource Inventory

Status: Phase 0 complete. `ResourceStore`, stable registry ownership, and resource-only component lifecycle hooks are implemented; exact-head CI validation is in progress.

This inventory records current ownership and the intended destination before resource migration begins. It is not a compatibility contract. The code remains the source of truth until each row is migrated and tested.

## Classification rules

- Session definition: immutable or registration-only identity shared by every registry in one running game session.
- Registry-local state: mutable state tied to one registry and destroyed with that registry.
- Scheduled-system-owned state: mutable state owned by a scheduled system and shared with registries through explicit binding or method arguments.
- External dependency binding: registry-local non-owning pointers to engine or game-owned services used by component lifecycle hooks.

## Current resource owners

The runtime currently has two type-indexed owners:

1. `Registry::Resources`, implemented by `ResourceStore`.
2. The private resource map inside `World`.

The migration removes the second owner. `Registry::Resources` remains the only registry resource owner.

## Production resource inventory

| Resource | Current owner | Registration site | Primary consumers | Final classification | Final owner | Construction requirement | Destruction requirement |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `ActiveCameraService` | `Registry::Resources` | default 3D registry and editor document initialization | camera setup, input, render extraction | registry-local state | each registry that owns camera selection | before camera activation | after entities stop referencing active camera ids |
| `StaticMeshComponentAssets` | `Registry::Resources` | default 3D registry and editor asset environment | `StaticMeshComponent` add and remove hooks | external dependency binding | registry `ResourceStore` | before any static mesh component is added or deserialized | after every static mesh component removal hook runs |
| `AudioSourceRuntime` | `Registry::Resources` | default 3D registry initialization | audio source and caption hooks, audio and caption systems | external dependency binding | registry `ResourceStore` | before audio source or caption components are added or deserialized | after every audio and caption removal hook runs |
| `PropagationOrderCache` | `World` resource map | lazy transform propagation | transform propagation | registry-local state | registry `ResourceStore` | lazy before first propagation is valid | before entity storage disappears is acceptable because it owns only cached ids and pointers; it must never outlive its registry |
| `PhysicsScene` | `World` resource map | lazy physics step | physics synchronization | registry-local state | registry `ResourceStore` | before the registry first participates in physics | before the system-owned `PhysicsWorld` it references is destroyed |
| `CharacterMoverPool` | `World` resource map | lazy character controller system | character controller reconciliation and drive | registry-local state | registry `ResourceStore` | before first character-controller physics pass | before the system-owned `PhysicsWorld` it references is destroyed |
| `AbilityActivationQueue` | `World` resource map | AbilityKit registration | ability activation producer and drain systems | registry-local state | registry `ResourceStore` | before a registry may enqueue activations | ordinary registry teardown; no external lifetime dependency |
| `GameplayTagRegistry` | `World` resource map | AbilityKit registration | tag matching, movement tag registration, serializers | session definition | global registry `ResourceStore` | before any tag ids are assigned or scene tag data is loaded | after every registry and serializer operation using tag ids is complete |
| `AttributeRegistry` | `World` resource map | AbilityKit registration | attribute registration, effect evaluation, serializers | session definition | global registry `ResourceStore` | before attribute ids are assigned or attribute data is loaded | after every registry using attribute ids is destroyed |
| `EffectRegistry` | `World` resource map | AbilityKit registration | effect application and movement ability definitions | session definition | global registry `ResourceStore` | before effect ids are assigned or abilities reference them | after every registry using effect ids is destroyed |
| `AbilityRegistry` | `World` resource map | AbilityKit registration | ability activation and movement definition registration | session definition | global registry `ResourceStore` | before ability ids are assigned or granted to entities | after every registry using ability ids is destroyed |
| `MovementTags` | `World` resource map | movement registration | grounding, jumping, locomotion, template input | session definition | global registry `ResourceStore` | after gameplay tag registry creation and before movement simulation | after every movement registry is destroyed |
| `MovementDefs` | `World` resource map | default movement ability registration | movement speed and jump ability consumers | session definition | global registry `ResourceStore` | after attribute, effect, and ability definitions are registered | after every registry using those ids is destroyed |
| `LocomotionModeRegistry` | `World` resource map | movement component registration | locomotion mode arbiter | session definition | global registry `ResourceStore` | after movement tags and marker component identities are registered | after every registry using mode entries is destroyed |

## Scheduled-system-owned state

| State | Current owner | Final owner | Registry relationship |
| --- | --- | --- | --- |
| `PhysicsWorld` | `PhysicsStepSystem` | unchanged | registry-local physics resources hold non-owning references or receive it explicitly |
| `CollisionShapeCache` | physics system or game composition | unchanged | passed to collision loading and physics setup explicitly |
| renderer asset caches | engine or game runtime assets | unchanged | `StaticMeshComponentAssets` stores non-owning registry bindings |
| `AudioService` | engine service | unchanged | `AudioSourceRuntime` stores a non-owning registry binding |
| `AudioClipCache` | engine or game runtime assets | unchanged | `AudioSourceRuntime` stores a non-owning registry binding |
| `CaptionRuntime` | engine runtime | unchanged | `AudioSourceRuntime` stores a non-owning registry binding |
| asset system and logging | engine services | unchanged | scene serialization context receives explicit pointers |

## Serializer dependencies

The gameplay tag and attribute serializers currently recover definition registries from `World`. They must instead receive definitions through `SceneSerializationContext`.

Initial explicit serializer dependencies:

- `GameplayTagRegistry* GameplayTags`
- `AttributeRegistry* Attributes`

Detached zone loading must capture the same session definition pointers used by synchronous loading. It must not construct zone-local copies.

## Editor boundary

Kyusu owns a transient `Registry`. Static mesh lifecycle bindings are installed in that registry's `ResourceStore` when an asset environment is attached.

The runtime resource migration must preserve the editor boundary mechanically:

- the editor registry receives its own `ResourceStore`
- editor component storage remains isolated from runtime registries
- no runtime-only service is introduced into the editor executable

Broader editor decomposition remains on `agent/editor-decomposition-plan`.

## Registration split required by the migration

Current AbilityKit and movement registration mix three responsibilities:

1. component storage registration
2. session definition registration
3. registry-local runtime state registration

They must become separate operations. A composition function may call them together when appropriate, but it must contain no hidden ownership policy.

Target shape:

```cpp
void RegisterAbilityComponents(EntityStore& entities);
void RegisterAbilityDefinitions(ResourceStore& sessionResources);
void RegisterAbilityRuntime(ResourceStore& registryResources);

void RegisterMovementComponents(EntityStore& entities);
void RegisterMovementDefinitions(ResourceStore& sessionResources);
```

## Phase 0 acceptance checks

Before moving non-lifecycle resources:

- exact-head CI must pass
- every production resource above has a final owner
- lifecycle resources have explicit construction and teardown requirements
- serializers have an explicit definition path
- detached loading has a declared session-definition source
- editor ownership is recorded without linking editor code into runtime

Any newly discovered production resource must be added here before it moves.
