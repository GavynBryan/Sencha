#include <script/ScriptSystemTick.h>

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
                              EntityId entity, std::uint64_t tick, float dt)
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
        return vm.Invoke(*linked.Module, functionIndex, host, options);
    }
}

void ScriptSystemTick::Step(World& world, std::uint64_t tickIndex, float dt)
{
    if (!world.HasResource<ScriptRuntime>())
    {
        return;
    }
    const ScriptRuntime& runtime = world.GetResource<ScriptRuntime>();

    // A `system` runs over the entities matching its `over` / `without`
    // signature; there is no per-entity attach component and no state. Systems
    // run in schedule (declaration) order; each flushes its own deferred effects
    // (and their reaction waves) before the next runs, so a later system sees an
    // earlier system's committed structural changes (per-system flush).
    for (std::uint32_t moduleIndex = 0; moduleIndex < runtime.Modules.size(); ++moduleIndex)
    {
        const ScriptLinkedModule& linked = runtime.Modules[moduleIndex];
        const ScriptModule& module = *linked.Module;
        for (std::uint32_t declIndex = 0; declIndex < module.Declarations.size(); ++declIndex)
        {
            const ScriptDeclaration& decl = module.Declarations[declIndex];
            if (decl.Kind != ScriptDeclKind::System)
            {
                continue;
            }
            const std::int32_t fixed = FindScriptCallback(module, decl, "fixed");
            if (fixed < 0)
            {
                continue;
            }
            CommandBuffer commands(world);
            WorldScriptHost host(world, commands, runtime);
            const std::vector<ComponentId>& signature = linked.DeclarationRequired[declIndex];
            const std::vector<ComponentId>& excluded = linked.DeclarationExcluded[declIndex];
            world.ForEachEntityMatching(signature, excluded, [&](EntityId entity) {
                const ScriptInvokeResult result =
                    Invoke(Vm, host, linked, moduleIndex, static_cast<std::uint32_t>(fixed),
                           entity, tickIndex, dt);
                if (Log != nullptr && !result.Ok())
                {
                    ReportScriptTrap(*Log, module, decl, result, tickIndex);
                }
            });
            commands.Flush();
        }
    }
}

void ScriptSystemTick::FixedLogic(FixedLogicContext& ctx)
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
