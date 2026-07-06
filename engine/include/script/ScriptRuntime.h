#pragma once

#include <ecs/ComponentId.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <gameplay_tags/GameplayTagId.h>
#include <script/ScriptModule.h>

#include <cstdint>
#include <memory>
#include <vector>

//=============================================================================
// ScriptRuntime (docs/plans/t-language-v1-spec.md, section E milestone 5)
//
// Per-World state for T execution. A cooked module is world-agnostic; linking
// it against a specific World resolves its symbolic binds (tag names,
// component/field paths, host imports) into runtime tables and registers the
// module's script-defined components. The result is a ScriptLinkedModule the
// bridges dispatch against.
//=============================================================================

// Per-entity attachment for the behavior bridge: which linked module and
// declaration drive this entity, and its current state (spec D.4). Trivially
// copyable ECS component. State is the interned index into the declaration's
// state list; -1 for a stateless behavior.
struct ScriptBehavior
{
    std::uint32_t LinkedModule = 0;
    std::uint32_t Declaration = 0;
    std::int32_t State = -1;
    std::uint32_t Spawned = 0; // 0 until the spawn callback has run
};
SENCHA_DECLARE_COMPONENT_TYPE(ScriptBehavior, "sencha.script_behavior");

// A field bind resolved to storage: which component, the byte offset of the
// leaf group, the storage scalar kind, and how many scalars (a Vec3 is 3).
struct ResolvedFieldBind
{
    ComponentId Component = InvalidComponentId;
    std::uint16_t Offset = 0;
    std::uint8_t Scalar = 0; // ScriptScalarKind of the stored value
    std::uint8_t SlotCount = 1;
    bool Ok = false;
};

struct ScriptLinkedModule
{
    std::shared_ptr<const ScriptModule> Module;
    std::vector<GameplayTagId> Tags;       // per BindTags index
    std::vector<ResolvedFieldBind> Fields; // per BindFields index
    std::vector<ComponentId> Components;   // per BindComponents index
    // Per BindComponents index: the module schema index for a script
    // component (used to marshal commands.add record images), or -1 for a
    // host component (not addable from a script in v1).
    std::vector<std::int32_t> ComponentSchemaIndex;
};

// World resource holding every linked module plus the deterministic world
// seed. Bridges look modules up by index (stored in ScriptBehavior).
struct ScriptRuntime
{
    std::vector<ScriptLinkedModule> Modules;
    std::uint64_t WorldSeed = 0;
};

// Per-entity state for one running script ability (spec D.4). AbilityKit has
// no per-tick active-ability loop, so the script ability bridge tracks its
// own instances: which linked module and ability declaration drive this
// owner, the current state, whether start has run, and the aim captured at
// activation (the AbilityContext prelude). One script ability per entity in
// v1. Trivially copyable ECS component.
struct ScriptAbilityState
{
    std::uint32_t LinkedModule = 0;
    std::uint32_t Declaration = 0;
    std::int32_t State = -1;
    std::uint32_t Started = 0;
    float AimOrigin[3] = { 0.0f, 0.0f, 0.0f };
    float AimDirection[3] = { 0.0f, 0.0f, 0.0f };
};
SENCHA_DECLARE_COMPONENT_TYPE(ScriptAbilityState, "sencha.script_ability_state");

// A pending script-ability activation. The bridge drains these at the start
// of its tick and installs a ScriptAbilityState on the owner. This is the
// script analog of AbilityActivationQueue; a game pushes a request when an
// AbilityKit ability that carries a script activates.
struct ScriptAbilityRequest
{
    EntityId Owner{};
    std::uint32_t LinkedModule = 0;
    std::uint32_t Declaration = 0;
    float AimOrigin[3] = { 0.0f, 0.0f, 0.0f };
    float AimDirection[3] = { 0.0f, 0.0f, 0.0f };
};

struct ScriptAbilityActivationQueue
{
    std::vector<ScriptAbilityRequest> Pending;
};

// EntityId <-> 64-bit register value. The VM carries entities as opaque
// 64-bit slots; this is the one place the packing is defined.
[[nodiscard]] inline std::uint64_t PackScriptEntity(EntityId entity)
{
    return static_cast<std::uint64_t>(entity.Index)
         | (static_cast<std::uint64_t>(entity.Generation) << 32);
}

[[nodiscard]] inline EntityId UnpackScriptEntity(std::uint64_t bits)
{
    return EntityId{ static_cast<EntityIndex>(bits & 0xFFFFFFFFu),
                     static_cast<std::uint32_t>(bits >> 32) };
}
