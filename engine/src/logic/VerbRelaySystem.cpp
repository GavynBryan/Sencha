#include <logic/VerbRelaySystem.h>

#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <core/console/ConsoleRegistry.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <logic/VerbRelay.h>
#include <logic/VerbRelayBindingStore.h>

#include <algorithm>
#include <utility>

VerbRelaySystem::VerbRelaySystem(VerbDispatcher& dispatcher, DataAssetCache& dataAssets,
                                 Logger& log)
    : Dispatcher(dispatcher)
    , DataAssets(dataAssets)
    , Log(log)
{
}

VerbAdmission VerbRelaySystem::Activate(EntityId relay,
                                        std::span<const VerbValue> inputs,
                                        InvocationId parent)
{
    if (!relay.IsValid())
    {
        ++Counts.Refused;
        return VerbAdmission::InvalidArguments;
    }
    if (Queue.size() >= QueueCapacity)
    {
        ++Counts.Refused;
        return VerbAdmission::QueueFull;
    }

    // Owned from here: the caller's span may be a stack array in a console
    // handler that returns before the drain.
    Request request;
    request.Relay = relay;
    request.Parent = parent;
    request.Inputs.assign(inputs.begin(), inputs.end());
    Queue.push_back(std::move(request));
    ++Counts.Admitted;
    return VerbAdmission::Accepted;
}

void VerbRelaySystem::SetCapacity(std::size_t capacity)
{
    QueueCapacity = std::max<std::size_t>(capacity, 1);
}

void VerbRelaySystem::Abandon(const Request& request, VerbAdmission why)
{
    ++Counts.Abandoned;
    if (VerbTraceRing* trace = Dispatcher.Trace())
    {
        VerbTraceRecord record;
        record.Event = VerbTraceEvent::Abandoned;
        record.Status = why;
        record.Parent = request.Parent;
        record.Producer = request.Relay;
        trace->Record(record);
    }
}

void VerbRelaySystem::FixedLogic(FixedLogicContext& ctx)
{
    if (Queue.empty())
        return;

    // Detached batch: what was here when the drain began. Anything admitted by
    // an operation running below lands in Queue and waits for the next tick.
    Batch.clear();
    std::swap(Batch, Queue);

    World& world = ctx.Entities;
    VerbRelayBindingStore* store = world.TryGetResource<VerbRelayBindingStore>();
    std::vector<std::string> errors;

    for (const Request& request : Batch)
    {
        if (!world.IsAlive(request.Relay))
        {
            Abandon(request, VerbAdmission::Refused);
            continue;
        }
        const VerbRelay* relay = world.TryGet<VerbRelay>(request.Relay);
        if (relay == nullptr)
        {
            Abandon(request, VerbAdmission::Refused);
            continue;
        }
        // A dormant zone's relay neither fires nor waits: the request was a
        // one-shot against the world as it was, and that world has moved on.
        if (!ctx.Partitions.Contains(world.GetEntityPartition(request.Relay)))
        {
            Abandon(request, VerbAdmission::Refused);
            continue;
        }
        const CompiledVerbBinding* binding =
            store != nullptr ? store->Resolve(*relay, world, &errors) : nullptr;
        for (const std::string& error : errors)
            Log.Error("relay: {}", error);
        errors.clear();
        if (binding == nullptr)
        {
            Abandon(request, VerbAdmission::UnresolvedBinding);
            continue;
        }

        const VerbInvocationResult result = Dispatcher.Invoke(
            *binding, request.Inputs,
            VerbInvocationSource{ .Parent = request.Parent, .Producer = request.Relay });
        if (result.Accepted())
            ++Counts.Invoked;
        else
            ++Counts.Abandoned;
    }
    Batch.clear();
}

void VerbRelaySystem::Shutdown()
{
    Queue.clear();
    Batch.clear();
}

void DisconnectVerbRelays(World& world)
{
    if (VerbRelayBindingStore* store = world.TryGetResource<VerbRelayBindingStore>())
        store->Clear();
}

VerbRelaySystem& RegisterVerbRelaySystem(EngineSchedule& schedule,
                                         World& world,
                                         VerbDispatcher& dispatcher,
                                         const AssetRegistry& assets,
                                         DataAssetCache& dataAssets,
                                         ConsoleRegistry& console,
                                         LoggingProvider& logging)
{
    if (world.TryGetResource<VerbRelayBindingStore>() == nullptr)
        world.AddResource<VerbRelayBindingStore>(assets, dataAssets);

    VerbRelaySystem& system = schedule.Register<VerbRelaySystem>(
        dispatcher, dataAssets, logging.GetLogger<VerbRelaySystem>());

    (void)console.RegisterCVar({
        .Name = "logic.relay.queue_capacity",
        .Owner = "engine",
        .Type = CVarType::Int,
        .DefaultValue = static_cast<std::int64_t>(system.Capacity()),
        .CurrentValue = static_cast<std::int64_t>(system.Capacity()),
        .Flags = CVarFlags::None,
        .Help = "How many relay activations may wait for the next fixed-logic drain. "
                "Overflow refuses the activation rather than dropping one already accepted.",
        .Source = { "engine" },
        .Min = 1.0,
        .Max = 65536.0,
        .OnChange = [&system](const CVarChangeContext& ctx) {
            system.SetCapacity(static_cast<std::size_t>(std::get<std::int64_t>(ctx.NewValue)));
        },
    });
    return system;
}
