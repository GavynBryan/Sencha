#pragma once

#include <ecs/EntityId.h>
#include <script/ScriptVm.h>

#include <cstdint>
#include <string_view>

struct FixedLogicContext;
class World;

//=============================================================================
// ScriptAbilitySystem (docs/plans/t-language-v1-spec.md, section E milestone 5)
//
// Drives T ability callbacks on the fixed tick. AbilityKit is data plus
// effects with no per-tick active-ability loop, so script abilities need
// their own instance tracking: this system drains pending activations
// (installing a ScriptAbilityState on each owner and running start), then
// ticks each active instance's per-state fixed callback, applying state
// transitions and the finish/cancel outcomes the callbacks request.
//
// start runs on the activation tick only; fixed begins the next tick, so an
// `enter` in start takes effect before the first fixed (spec D.4). finish and
// cancel run their named callbacks (if present), then the instance is removed.
//=============================================================================
class ScriptAbilitySystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);

    // Runs one world's script abilities for a tick. Exposed for tests.
    void Step(World& world, std::uint64_t tickIndex, float dt);

private:
    ScriptVm Vm;
};

// Registers the ScriptAbilityState component and the activation queue in a
// world, before any entity is created. Idempotent. Requires the ScriptRuntime
// resource (RegisterScriptRuntime).
void RegisterScriptAbilities(World& world);

// Finds the declaration index of an ability by name in a linked module, or -1.
[[nodiscard]] std::int32_t FindScriptAbilityDeclaration(const World& world,
                                                        std::uint32_t linkedModuleIndex,
                                                        std::string_view abilityName);

// Enqueues a script-ability activation for `owner`. Processed at the start of
// the next ScriptAbilitySystem tick.
void RequestScriptAbility(World& world, EntityId owner, std::uint32_t linkedModuleIndex,
                          std::uint32_t declaration,
                          const float aimOrigin[3], const float aimDirection[3]);
