#pragma once

#include <assets/script/ScriptCache.h>
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

// One cue a script emitted this tick: the content hash of the cue asset path
// (stable across builds, unlike a runtime id) and an optional world position.
// A presentation system drains the buffer and resolves the hash to a cue
// asset; the script layer never plays audio or VFX directly.
struct ScriptCueEvent
{
    std::uint64_t CueHash = 0;
    float Position[3] = { 0.0f, 0.0f, 0.0f };
    std::uint8_t HasPosition = 0;
};

// Per-entity ring of cue events (spec section C, cue.fire / cue_at). Trivially
// copyable ECS component; drops the oldest on overflow.
struct ScriptCueBuffer
{
    static constexpr std::uint8_t Capacity = 8;
    ScriptCueEvent Events[Capacity];
    std::uint8_t Count = 0;

    void Push(const ScriptCueEvent& event)
    {
        if (Count < Capacity)
        {
            Events[Count++] = event;
        }
        else
        {
            for (std::uint8_t i = 1; i < Capacity; ++i)
            {
                Events[i - 1] = Events[i];
            }
            Events[Capacity - 1] = event;
        }
    }
};
SENCHA_DECLARE_COMPONENT_TYPE(ScriptCueBuffer, "sencha.script_cue_buffer");

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
    // Parallel to Modules: the asset handle that loaded each module, so the
    // scene resolve step can map a ScriptSource's handle to its linked module.
    // Invalid for modules linked directly (tests) rather than via an asset.
    std::vector<ScriptHandle> ModuleHandles;
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
