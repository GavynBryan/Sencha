#include <script/ScriptObserverDispatch.h>

#include <core/logging/LoggingProvider.h>
#include <ecs/CommandBuffer.h>
#include <ecs/TransitionDispatch.h>
#include <ecs/World.h>
#include <script/ScriptRuntime.h>
#include <script/ScriptTrapReport.h>
#include <script/WorldScriptHost.h>

#include <bit>

void ScriptObserverDispatch::Register(World& world, ComponentId component, const Binding& binding)
{
    const std::size_t idx = static_cast<std::size_t>(component);
    if (ByComponent.size() <= idx)
    {
        ByComponent.resize(idx + 1);
    }
    const bool firstForComponent = ByComponent[idx].empty();
    ByComponent[idx].push_back(binding);

    if (firstForComponent)
    {
        TransitionDispatch dispatch;
        dispatch.OnAdd = &OnAddCb;
        dispatch.OnRemove = &OnRemoveCb;
        dispatch.Context = this;
        world.RegisterTransitionDispatch(component, dispatch);
    }
}

void ScriptObserverDispatch::OnAddCb(World& world, EntityId entity, ComponentId component,
                                     const void*, void* context)
{
    static_cast<ScriptObserverDispatch*>(context)->Fire(world, entity, component, /*isAdd*/ true);
}

void ScriptObserverDispatch::OnRemoveCb(World& world, EntityId entity, ComponentId component,
                                        const void*, void* context)
{
    static_cast<ScriptObserverDispatch*>(context)->Fire(world, entity, component, /*isAdd*/ false);
}

void ScriptObserverDispatch::Fire(World& world, EntityId entity, ComponentId component, bool isAdd)
{
    const std::size_t idx = static_cast<std::size_t>(component);
    if (idx >= ByComponent.size() || ByComponent[idx].empty())
    {
        return;
    }
    // Observer effects defer into the active reaction scope (the next wave). With
    // no scope (a native immediate mutation), there is nowhere to defer; that
    // boundary is not exercised by the simulation, which routes structural change
    // through the CommandBuffer whose flush owns the wave loop.
    CommandBuffer* reactions = world.ActiveReactions();
    if (reactions == nullptr || !world.HasResource<ScriptRuntime>())
    {
        return;
    }
    const ScriptRuntime& runtime = world.GetResource<ScriptRuntime>();
    WorldScriptHost host(world, *reactions, runtime);

    for (const Binding& binding : ByComponent[idx])
    {
        const std::int32_t fn = isAdd ? binding.OnAddFn : binding.OnRemoveFn;
        if (fn < 0)
        {
            continue;
        }
        const ScriptLinkedModule& linked = runtime.Modules[binding.LinkedModule];
        host.Begin(binding.LinkedModule, entity, /*tick*/ 0);
        const std::uint64_t args[1] = { PackScriptEntity(entity) };
        ScriptInvokeOptions options;
        options.Args = args;
        options.Imports = linked.Imports;
        const ScriptInvokeResult result =
            Vm.Invoke(*linked.Module, static_cast<std::uint32_t>(fn), host, options);
        if (Log != nullptr && !result.Ok())
        {
            ReportScriptTrap(*Log, *linked.Module,
                             linked.Module->Declarations[binding.Declaration], result, /*tick*/ 0);
        }
    }
}
