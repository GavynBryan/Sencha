#include <script/ScriptAbilitySystem.h>

#include <app/GameContexts.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/CommandBuffer.h>
#include <ecs/World.h>
#include <script/ScriptCallbacks.h>
#include <script/ScriptRuntime.h>
#include <script/ScriptTrapReport.h>
#include <script/WorldScriptHost.h>

#include <bit>
#include <vector>

namespace
{
    // Builds the AbilityContext prelude: owner, tick, dt, aim_origin (3),
    // aim_direction (3). Aim stored as f32; the VM registers are f64.
    void BuildArgs(std::uint64_t args[9], EntityId owner, std::uint64_t tick, float dt,
                   const float aimOrigin[3], const float aimDirection[3])
    {
        args[0] = PackScriptEntity(owner);
        args[1] = tick;
        args[2] = std::bit_cast<std::uint64_t>(static_cast<double>(dt));
        for (int i = 0; i < 3; ++i)
        {
            args[3 + i] = std::bit_cast<std::uint64_t>(static_cast<double>(aimOrigin[i]));
            args[6 + i] = std::bit_cast<std::uint64_t>(static_cast<double>(aimDirection[i]));
        }
    }

    // Runs a callback and returns its outcome; applies a pending state
    // transition to `stateInOut`. A trap cancels the ability and is logged.
    ScriptAbilityOutcome RunCallback(ScriptVm& vm, WorldScriptHost& host,
                                     const ScriptLinkedModule& linked, std::uint32_t linkedIndex,
                                     std::int32_t functionIndex, const std::uint64_t args[9],
                                     EntityId owner, std::uint64_t tick, std::int32_t& stateInOut,
                                     LoggingProvider* log, const ScriptDeclaration& decl)
    {
        if (functionIndex < 0)
        {
            return ScriptAbilityOutcome::None;
        }
        host.Begin(linkedIndex, owner, tick);
        ScriptInvokeOptions options;
        options.Args = std::span<const std::uint64_t>(args, 9);
        options.Imports = linked.Imports;
        const ScriptInvokeResult result =
            vm.Invoke(*linked.Module, static_cast<std::uint32_t>(functionIndex), host, options);
        if (result.Ok() && result.PendingState >= 0)
        {
            stateInOut = result.PendingState;
        }
        if (!result.Ok())
        {
            if (log != nullptr)
            {
                ReportScriptTrap(*log, *linked.Module, decl, result, tick);
            }
            return ScriptAbilityOutcome::Cancel;
        }
        return host.Outcome();
    }
}

void ScriptAbilitySystem::Step(World& world, std::uint64_t tickIndex, float dt)
{
    if (!world.HasResource<ScriptRuntime>() || !world.IsRegistered<ScriptAbilityState>())
    {
        return;
    }
    const ScriptRuntime& runtime = world.GetResource<ScriptRuntime>();

    // 1. Install newly activated abilities (outside any query iteration).
    if (auto* queue = world.TryGetResource<ScriptAbilityActivationQueue>())
    {
        std::vector<ScriptAbilityRequest> pending;
        pending.swap(queue->Pending);
        for (const ScriptAbilityRequest& request : pending)
        {
            if (!world.IsAlive(request.Owner)
                || world.HasComponent<ScriptAbilityState>(request.Owner))
            {
                continue; // one script ability per entity in v1
            }
            ScriptAbilityState state;
            state.LinkedModule = request.LinkedModule;
            state.Declaration = request.Declaration;
            for (int i = 0; i < 3; ++i)
            {
                state.AimOrigin[i] = request.AimOrigin[i];
                state.AimDirection[i] = request.AimDirection[i];
            }
            world.AddComponent(request.Owner, state);
        }
    }

    CommandBuffer commands(world);
    WorldScriptHost host(world, commands, runtime);
    std::vector<EntityId> toRemove;

    world.ForEachComponent<ScriptAbilityState>([&](EntityId owner, ScriptAbilityState& ability) {
        if (ability.LinkedModule >= runtime.Modules.size())
        {
            return;
        }
        const ScriptLinkedModule& linked = runtime.Modules[ability.LinkedModule];
        const ScriptModule& module = *linked.Module;
        if (ability.Declaration >= module.Declarations.size())
        {
            return;
        }
        const ScriptDeclaration& decl = module.Declarations[ability.Declaration];

        std::uint64_t args[9];
        BuildArgs(args, owner, tickIndex, dt, ability.AimOrigin, ability.AimDirection);

        std::int32_t state = ability.State;
        ScriptAbilityOutcome outcome = ScriptAbilityOutcome::None;

        if (ability.Started == 0)
        {
            // Activation tick: start only. fixed begins next tick so an
            // `enter` in start settles first.
            ability.Started = 1;
            outcome = RunCallback(Vm, host, linked, ability.LinkedModule,
                                  FindScriptCallback(module, decl, "start"), args, owner, tickIndex,
                                  state, Log, decl);
        }
        else
        {
            outcome = RunCallback(Vm, host, linked, ability.LinkedModule,
                                  FindScriptFixedCallback(module, decl, state), args, owner,
                                  tickIndex, state, Log, decl);
        }
        ability.State = state;

        if (outcome == ScriptAbilityOutcome::Finish)
        {
            RunCallback(Vm, host, linked, ability.LinkedModule,
                        FindScriptCallback(module, decl, "finish"), args, owner, tickIndex, state,
                        Log, decl);
            toRemove.push_back(owner);
        }
        else if (outcome == ScriptAbilityOutcome::Cancel)
        {
            RunCallback(Vm, host, linked, ability.LinkedModule,
                        FindScriptCallback(module, decl, "cancel"), args, owner, tickIndex, state,
                        Log, decl);
            toRemove.push_back(owner);
        }
    });

    // Structural changes the callbacks requested, then the bridge's own
    // instance cleanup, both outside the iteration above.
    commands.Flush();
    for (const EntityId owner : toRemove)
    {
        if (world.IsAlive(owner) && world.HasComponent<ScriptAbilityState>(owner))
        {
            world.RemoveComponent<ScriptAbilityState>(owner);
        }
    }
}

void ScriptAbilitySystem::FixedLogic(FixedLogicContext& ctx)
{
    const auto tick = static_cast<std::uint64_t>(ctx.Time.TickIndex);
    const float dt = static_cast<float>(ctx.Time.DeltaSeconds);
    for (Registry* registry : ctx.ActiveRegistries)
    {
        if (registry != nullptr)
        {
            Step(registry->Components, tick, dt);
        }
    }
}

void RegisterScriptAbilities(World& world)
{
    if (!world.IsRegistered<ScriptAbilityState>())
    {
        world.RegisterComponent<ScriptAbilityState>();
    }
    if (!world.HasResource<ScriptAbilityActivationQueue>())
    {
        world.AddResource<ScriptAbilityActivationQueue>();
    }
}

std::int32_t FindScriptAbilityDeclaration(const World& world, std::uint32_t linkedModuleIndex,
                                          std::string_view abilityName)
{
    const ScriptRuntime* runtime = world.TryGetResource<ScriptRuntime>();
    if (runtime == nullptr || linkedModuleIndex >= runtime->Modules.size())
    {
        return -1;
    }
    const ScriptModule& module = *runtime->Modules[linkedModuleIndex].Module;
    for (std::size_t i = 0; i < module.Declarations.size(); ++i)
    {
        const ScriptDeclaration& decl = module.Declarations[i];
        if (decl.Kind == ScriptDeclKind::Ability && module.GetString(decl.Name) == abilityName)
        {
            return static_cast<std::int32_t>(i);
        }
    }
    return -1;
}

void RequestScriptAbility(World& world, EntityId owner, std::uint32_t linkedModuleIndex,
                          std::uint32_t declaration, const float aimOrigin[3],
                          const float aimDirection[3])
{
    if (!world.HasResource<ScriptAbilityActivationQueue>())
    {
        world.AddResource<ScriptAbilityActivationQueue>();
    }
    ScriptAbilityRequest request;
    request.Owner = owner;
    request.LinkedModule = linkedModuleIndex;
    request.Declaration = declaration;
    for (int i = 0; i < 3; ++i)
    {
        request.AimOrigin[i] = aimOrigin != nullptr ? aimOrigin[i] : 0.0f;
        request.AimDirection[i] = aimDirection != nullptr ? aimDirection[i] : 0.0f;
    }
    world.GetResource<ScriptAbilityActivationQueue>().Pending.push_back(request);
}
