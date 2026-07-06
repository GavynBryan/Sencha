#pragma once

#include <script/ScriptVm.h>

#include <cstdint>

struct FixedLogicContext;
class World;

//=============================================================================
// ScriptBehaviorSystem (docs/plans/t-language-v1-spec.md, section E milestone 5)
//
// Invokes T behavior callbacks on the fixed tick. For each entity carrying a
// ScriptBehavior, it runs the spawn callback once (lazily, the first time the
// entity is seen) then the fixed callback, dispatching through the VM against
// a WorldScriptHost bound to that entity. Callbacks are system details, not
// scheduled entities: this is one scheduled system, serial, chunk order.
//
// Structural changes a callback requests go through a CommandBuffer flushed at
// the end of the tick; component field writes are immediate. State transitions
// a callback returns (spec D.4) are applied to ScriptBehavior.State after the
// callback returns.
//=============================================================================
class ScriptBehaviorSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);

    // Runs one world's behaviors for a tick. Exposed so tests can drive it
    // without a full schedule.
    void Step(World& world, std::uint64_t tickIndex, float dt);

private:
    ScriptVm Vm;
};

// Registers the ScriptBehavior component and the ScriptRuntime resource in a
// world, before any entity is created. Idempotent. GameplayTagContainer (for
// tag ops) is registered by RegisterAbilityKit when abilities are in use.
void RegisterScriptRuntime(World& world, std::uint64_t worldSeed = 0);
