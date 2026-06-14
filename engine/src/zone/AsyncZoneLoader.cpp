#include <zone/AsyncZoneLoader.h>

#include <core/assets/AssetPreloader.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/registry/Registry.h>
#include <zone/ZoneRuntime.h>

#include <algorithm>
#include <cassert>
#include <memory>
#include <utility>

AsyncZoneLoader::AsyncZoneLoader(AsyncTaskQueue& tasks, ZoneRuntime& zones, RuntimeFrameLoop& runtime)
    : Tasks(tasks)
    , Zones(zones)
    , Runtime(runtime)
{
}

AsyncTaskHandle AsyncZoneLoader::BeginLoad(ZoneId zone, BuildFn build, ZoneParticipation participation)
{
    return BeginLoad(zone, std::move(build), FinalizeFn{}, participation);
}

AsyncTaskHandle AsyncZoneLoader::BeginLoad(ZoneId zone, BuildFn build, FinalizeFn finalize,
                                           ZoneParticipation participation)
{
    return BeginLoad(zone, std::move(build), std::move(finalize), participation, nullptr);
}

AsyncTaskHandle AsyncZoneLoader::BeginLoad(ZoneId zone, BuildFn build, FinalizeFn finalize,
                                           ZoneParticipation participation,
                                           std::shared_ptr<AssetPreload> assets)
{
    assert(zone.IsValid() && "AsyncZoneLoader::BeginLoad: zone id must be valid");
    assert(build && "AsyncZoneLoader::BeginLoad: build callback must not be empty");
    assert(!Zones.IsZoneLoaded(zone) && "AsyncZoneLoader::BeginLoad: zone is already loaded");
    assert(!IsLoading(zone) && "AsyncZoneLoader::BeginLoad: zone load is already in flight");

    const RegistryId registryId = Zones.ReserveRegistryId();

    AsyncTaskHandle handle = Tasks.Submit<std::unique_ptr<Registry>>(
        // Work, on a task thread: the registry is detached — solely owned by
        // the task — so the build needs no synchronization.
        [registryId, zone, build = std::move(build)]() -> std::unique_ptr<Registry>
        {
            auto registry = std::make_unique<Registry>(MakeZoneRegistry(registryId, zone));
            build(*registry);
            return registry;
        },
        // Commit, on the owner thread at the drain point: publish-by-handoff.
        // A cancelled preload counts as complete — the attach proceeds and
        // finalize's synchronous fallback resolves whatever is missing.
        [this, zone, participation, finalize = std::move(finalize), assets](
            std::unique_ptr<Registry> registry) mutable
        {
            if (assets && !assets->IsComplete() && !assets->IsCancelled())
            {
                // Defer the attach to the preload's last asset commit. The
                // shared wrappers exist because std::function requires
                // copyable captures; everything still runs at the drain
                // point on the owner thread.
                auto deferredRegistry =
                    std::make_shared<std::unique_ptr<Registry>>(std::move(registry));
                auto deferredFinalize = std::make_shared<FinalizeFn>(std::move(finalize));
                assets->SetOnComplete(
                    [this, zone, participation, deferredRegistry, deferredFinalize, assets]()
                    {
                        AttachAndFinalize(zone, std::move(*deferredRegistry),
                                          *deferredFinalize, participation, assets);
                    });
                return;
            }

            AttachAndFinalize(zone, std::move(registry), finalize, participation, assets);
        });

    InFlight.push_back(InFlightLoad{ zone, handle, std::move(assets) });
    return handle;
}

void AsyncZoneLoader::AttachAndFinalize(ZoneId zone, std::unique_ptr<Registry> registry,
                                        FinalizeFn& finalize, ZoneParticipation participation,
                                        const std::shared_ptr<AssetPreload>& assets)
{
    RemoveInFlight(zone);
    Registry& attached = Zones.AttachZone(std::move(registry), participation);
    if (finalize)
    {
        finalize(attached);
    }
    // The preload's handles were scaffolding: finalize's entities hold their
    // own references now (component traits), so the preload lets go.
    if (assets)
    {
        assets->ReleaseAll();
    }
    // A dormant attach (no participation) is invisible to the frame —
    // no discontinuity occurred, definitionally. This is the seamless
    // room-preload path; activation later is the game's decision
    // (SetParticipation, plus Teleport if the camera cuts).
    if (participation.Any())
    {
        Runtime.MarkTemporalDiscontinuity(TemporalDiscontinuityReason::ZoneLoad);
    }
}

bool AsyncZoneLoader::IsLoading(ZoneId zone) const
{
    return std::any_of(InFlight.begin(), InFlight.end(),
                       [zone](const InFlightLoad& load) { return load.Zone == zone; });
}

bool AsyncZoneLoader::CancelLoad(ZoneId zone)
{
    auto it = std::find_if(InFlight.begin(), InFlight.end(),
                           [zone](const InFlightLoad& load) { return load.Zone == zone; });
    if (it == InFlight.end())
        return false;

    if (!Tasks.Cancel(it->Handle))
        return false;  // build is mid-flight; retry once it finishes

    InFlight.erase(it);
    return true;
}

void AsyncZoneLoader::RemoveInFlight(ZoneId zone)
{
    InFlight.erase(std::remove_if(InFlight.begin(), InFlight.end(),
                                  [zone](const InFlightLoad& load) { return load.Zone == zone; }),
                   InFlight.end());
}
