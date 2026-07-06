#include <script/ScriptBehaviorSystem.h>

#include <app/GameContexts.h>
#include <ecs/CommandBuffer.h>
#include <ecs/World.h>
#include <script/ScriptRuntime.h>
#include <script/WorldScriptHost.h>

#include <bit>
#include <string>

namespace
{
    // Finds a callback's function index in a declaration, or -1.
    std::int32_t FindCallback(const ScriptModule& module, const ScriptDeclaration& decl,
                              std::string_view name)
    {
        for (const ScriptCallbackDef& callback : decl.Callbacks)
        {
            if (module.GetString(callback.Name) == name)
            {
                return static_cast<std::int32_t>(callback.FunctionIndex);
            }
        }
        return -1;
    }

    // The callback name for the fixed tick given the current state: a
    // per-state "State.fixed" when in a state, else the bare "fixed".
    std::int32_t FixedCallback(const ScriptModule& module, const ScriptDeclaration& decl,
                               std::int32_t state)
    {
        if (state >= 0 && static_cast<std::size_t>(state) < decl.States.size())
        {
            const std::string qualified =
                std::string(module.GetString(decl.States[state])) + ".fixed";
            if (const std::int32_t fn = FindCallback(module, decl, qualified); fn >= 0)
            {
                return fn;
            }
        }
        return FindCallback(module, decl, "fixed");
    }

    void Invoke(ScriptVm& vm, WorldScriptHost& host, const ScriptModule& module,
                std::uint32_t linkedIndex, std::uint32_t functionIndex, EntityId entity,
                std::uint64_t tick, float dt, std::int32_t& stateInOut)
    {
        host.Begin(linkedIndex, entity, tick);
        const std::uint64_t args[3] = {
            PackScriptEntity(entity),
            tick,
            std::bit_cast<std::uint64_t>(static_cast<double>(dt)),
        };
        ScriptInvokeOptions options;
        options.Args = args;
        const ScriptInvokeResult result = vm.Invoke(module, functionIndex, host, options);
        if (result.Ok() && result.PendingState >= 0)
        {
            stateInOut = result.PendingState;
        }
        // A trap ends this callback; behaviors resume next tick (spec D.4).
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
        if (behavior.Spawned == 0)
        {
            behavior.Spawned = 1;
            if (const std::int32_t spawn = FindCallback(module, decl, "spawn"); spawn >= 0)
            {
                Invoke(Vm, host, module, behavior.LinkedModule,
                       static_cast<std::uint32_t>(spawn), entity, tickIndex, dt, state);
            }
        }
        if (const std::int32_t fixed = FixedCallback(module, decl, state); fixed >= 0)
        {
            Invoke(Vm, host, module, behavior.LinkedModule,
                   static_cast<std::uint32_t>(fixed), entity, tickIndex, dt, state);
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
