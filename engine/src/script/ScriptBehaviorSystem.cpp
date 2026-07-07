#include <script/ScriptBehaviorSystem.h>

#include <app/GameContexts.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/CommandBuffer.h>
#include <ecs/World.h>
#include <script/ScriptCallbacks.h>
#include <script/ScriptRuntime.h>
#include <script/ScriptTrapReport.h>
#include <script/WorldScriptHost.h>

#include <bit>

namespace
{
    ScriptInvokeResult Invoke(ScriptVm& vm, WorldScriptHost& host, const ScriptLinkedModule& linked,
                              std::uint32_t linkedIndex, std::uint32_t functionIndex,
                              EntityId entity, std::uint64_t tick, float dt,
                              std::int32_t& stateInOut)
    {
        host.Begin(linkedIndex, entity, tick);
        const std::uint64_t args[3] = {
            PackScriptEntity(entity),
            tick,
            std::bit_cast<std::uint64_t>(static_cast<double>(dt)),
        };
        ScriptInvokeOptions options;
        options.Args = args;
        options.Imports = linked.Imports;
        const ScriptInvokeResult result = vm.Invoke(*linked.Module, functionIndex, host, options);
        if (result.Ok() && result.PendingState >= 0)
        {
            stateInOut = result.PendingState;
        }
        // A trap ends this callback; behaviors resume next tick.
        return result;
    }
}

void ScriptBehaviorSystem::Step(World& world, std::uint64_t tickIndex, float dt)
{
    if (!world.HasResource<ScriptRuntime>() || !world.IsRegistered<ScriptBehavior>())
    {
        return;
    }
    const ScriptRuntime& runtime = world.GetResource<ScriptRuntime>();

    CommandBuffer commands(world);
    WorldScriptHost host(world, commands, runtime);

    world.ForEachComponent<ScriptBehavior>([&](EntityId entity, ScriptBehavior& behavior) {
        if (behavior.LinkedModule >= runtime.Modules.size())
        {
            return;
        }
        const ScriptLinkedModule& linked = runtime.Modules[behavior.LinkedModule];
        const ScriptModule& module = *linked.Module;
        if (behavior.Declaration >= module.Declarations.size())
        {
            return;
        }
        const ScriptDeclaration& decl = module.Declarations[behavior.Declaration];

        std::int32_t state = behavior.State;
        // Spawned is set before invoking, so a trapping spawn is not retried
        // (and does not re-log) every tick.
        if (behavior.Spawned == 0)
        {
            behavior.Spawned = 1;
            if (const std::int32_t spawn = FindScriptCallback(module, decl, "spawn"); spawn >= 0)
            {
                const ScriptInvokeResult result =
                    Invoke(Vm, host, linked, behavior.LinkedModule,
                           static_cast<std::uint32_t>(spawn), entity, tickIndex, dt, state);
                if (Log != nullptr && !result.Ok())
                {
                    ReportScriptTrap(*Log, module, decl, result, tickIndex);
                }
            }
        }
        if (const std::int32_t fixed = FindScriptFixedCallback(module, decl, state); fixed >= 0)
        {
            const ScriptInvokeResult result =
                Invoke(Vm, host, linked, behavior.LinkedModule, static_cast<std::uint32_t>(fixed),
                       entity, tickIndex, dt, state);
            if (Log != nullptr && !result.Ok())
            {
                ReportScriptTrap(*Log, module, decl, result, tickIndex);
            }
        }
        behavior.State = state;
    });

    // Structural changes the callbacks requested apply at the tick boundary,
    // outside the query iteration above.
    commands.Flush();
}

void ScriptBehaviorSystem::FixedLogic(FixedLogicContext& ctx)
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

void RegisterScriptRuntime(World& world, std::uint64_t worldSeed)
{
    if (!world.IsRegistered<ScriptBehavior>())
    {
        world.RegisterComponent<ScriptBehavior>();
    }
    if (!world.IsRegistered<ScriptCueBuffer>())
    {
        world.RegisterComponent<ScriptCueBuffer>();
    }
    if (!world.HasResource<ScriptRuntime>())
    {
        world.AddResource<ScriptRuntime>().WorldSeed = worldSeed;
    }
    else
    {
        world.GetResource<ScriptRuntime>().WorldSeed = worldSeed;
    }
}
