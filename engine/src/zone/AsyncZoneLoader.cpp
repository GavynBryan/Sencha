#include <zone/AsyncZoneLoader.h>

#include <assets/runtime/AssetPreloader.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/WorldComponentSchema.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/RuntimeWorld.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <zone/ZonePackageImporter.h>

#include <algorithm>
#include <cassert>
#include <memory>
#include <utility>

AsyncZoneLoader::AsyncZoneLoader(
    AsyncTaskQueue& tasks,
    RuntimeWorld& world,
    const WorldComponentSchema& schema,
    const ComponentSerializerRegistry& serializers,
    SceneSerializationContext& sceneContext,
    RuntimeFrameLoop& runtime)
    : Tasks(tasks)
    , RuntimeWorldState(world)
    , Schema(schema)
    , Serializers(serializers)
    , SceneContext(sceneContext)
    , Runtime(runtime)
    , Log(sceneContext.Logging->GetLogger<AsyncZoneLoader>())
{
}

AsyncTaskHandle AsyncZoneLoader::BeginLoad(
    ZoneId zone,
    BuildFn build,
    ZoneParticipation participation)
{
    return BeginLoad(
        zone,
        std::move(build),
        FinalizeFn{},
        participation);
}

AsyncTaskHandle AsyncZoneLoader::BeginLoad(
    ZoneId zone,
    BuildFn build,
    FinalizeFn finalize,
    ZoneParticipation participation)
{
    return BeginLoad(
        zone,
        std::move(build),
        std::move(finalize),
        participation,
        nullptr);
}

AsyncTaskHandle AsyncZoneLoader::BeginLoad(
    ZoneId zone,
    BuildFn build,
    FinalizeFn finalize,
    ZoneParticipation participation,
    std::shared_ptr<AssetPreload> assets)
{
    assert(zone.IsValid() && "AsyncZoneLoader::BeginLoad: zone id must be valid");
    assert(build && "AsyncZoneLoader::BeginLoad: build callback must not be empty");
    assert(RuntimeWorldState.FindZone(zone) == nullptr
           && "AsyncZoneLoader::BeginLoad: zone is already loaded or importing");
    assert(!IsLoading(zone)
           && "AsyncZoneLoader::BeginLoad: zone load is already in flight");

    AsyncTaskHandle handle = Tasks.Submit<std::unique_ptr<ZoneLoadPackage>>(
        // Work, on a task thread: package-local identity and owned CPU payloads
        // need no synchronization with the live entity world.
        [zone, build = std::move(build)]() mutable {
            auto package = std::make_unique<ZoneLoadPackage>(zone);
            build(*package);
            return package;
        },
        // Commit, on the owner thread at the drain point. A cancelled preload
        // counts as complete; decoding may use its synchronous fallback.
        [this, zone, participation, finalize = std::move(finalize), assets](
            std::unique_ptr<ZoneLoadPackage> package) mutable {
            if (assets && !assets->IsComplete() && !assets->IsCancelled())
            {
                auto deferredPackage =
                    std::make_shared<std::unique_ptr<ZoneLoadPackage>>(
                        std::move(package));
                auto deferredFinalize =
                    std::make_shared<FinalizeFn>(std::move(finalize));
                assets->SetOnComplete(
                    [this,
                     zone,
                     participation,
                     deferredPackage,
                     deferredFinalize,
                     assets]() mutable {
                        ImportAndFinalize(
                            zone,
                            std::move(*deferredPackage),
                            *deferredFinalize,
                            participation,
                            assets);
                    });
                return;
            }

            ImportAndFinalize(
                zone,
                std::move(package),
                finalize,
                participation,
                assets);
        });

    InFlight.push_back(InFlightLoad{ zone, handle, std::move(assets) });
    return handle;
}

void AsyncZoneLoader::ImportAndFinalize(
    ZoneId zone,
    std::unique_ptr<ZoneLoadPackage> package,
    FinalizeFn& finalize,
    ZoneParticipation participation,
    const std::shared_ptr<AssetPreload>& assets)
{
    RemoveInFlight(zone);

    ZoneImportError importError;
    if (package == nullptr
        || !ImportZonePackageHidden(
            RuntimeWorldState,
            Schema,
            *package,
            Serializers,
            SceneContext,
            &importError))
    {
        RecordFailure(
            zone,
            ZoneLoadStage::Import,
            package == nullptr ? "package was not produced"
                               : std::move(importError.Message));
        if (assets)
            assets->ReleaseAll();
        return;
    }

    RuntimeZoneRecord* record = RuntimeWorldState.FindZone(zone);
    assert(record != nullptr
           && record->State == RuntimeZoneLoadState::Importing);

    if (finalize && !finalize(RuntimeWorldState, *record))
    {
        (void)RuntimeWorldState.CancelZoneImport(zone);
        RecordFailure(
            zone,
            ZoneLoadStage::Finalize,
            "finalize declined publication");
        if (assets)
            assets->ReleaseAll();
        return;
    }

    if (!RuntimeWorldState.PublishZone(zone, participation))
    {
        (void)RuntimeWorldState.CancelZoneImport(zone);
        RecordFailure(
            zone,
            ZoneLoadStage::Publish,
            "publication rejected the imported partition");
        if (assets)
            assets->ReleaseAll();
        return;
    }

    // A zone that loads after a previous refusal is healthy again; leaving the
    // record would suppress every future reload of a working zone.
    (void)ClearFailure(zone);

    // The preload's handles were scaffolding: published entities and backend
    // records hold their own references now.
    if (assets)
        assets->ReleaseAll();

    // Dormant preload is the genre-critical seamless path. Activation later is
    // the game's participation decision; only immediately participating loads
    // invalidate presentation history.
    if (participation.Any())
        Runtime.MarkTemporalDiscontinuity(TemporalDiscontinuityReason::ZoneLoad);
}

bool AsyncZoneLoader::IsLoading(ZoneId zone) const
{
    return std::any_of(
        InFlight.begin(),
        InFlight.end(),
        [zone](const InFlightLoad& load) { return load.Zone == zone; });
}

AsyncTaskHandle AsyncZoneLoader::RequestDestroy(ZoneId zone)
{
    return Tasks.Submit<int>(
        [] { return 0; },
        [this, zone](int) {
            if (RuntimeWorldState.RequestDetach(zone))
                RuntimeWorldState.FlushLifecycleRequests();
        });
}

bool AsyncZoneLoader::CancelLoad(ZoneId zone)
{
    auto it = std::find_if(
        InFlight.begin(),
        InFlight.end(),
        [zone](const InFlightLoad& load) { return load.Zone == zone; });
    if (it == InFlight.end())
        return false;

    if (!Tasks.Cancel(it->Handle))
        return false; // build is mid-flight; retry once it finishes

    InFlight.erase(it);
    return true;
}

void AsyncZoneLoader::RecordFailure(
    ZoneId zone,
    ZoneLoadStage stage,
    std::string message)
{
    for (ZoneLoadFailure& existing : Failures_)
    {
        if (existing.Zone != zone)
            continue;
        existing.Stage = stage;
        existing.Message = std::move(message);
        return;
    }

    Log.Error(
        "zone {:016x} failed to load at {}: {}",
        zone.Value,
        ZoneLoadStageName(stage),
        message);
    Failures_.push_back(
        ZoneLoadFailure{ zone, stage, std::move(message) });
}

const ZoneLoadFailure* AsyncZoneLoader::FindFailure(ZoneId zone) const
{
    for (const ZoneLoadFailure& failure : Failures_)
        if (failure.Zone == zone)
            return &failure;
    return nullptr;
}

bool AsyncZoneLoader::ClearFailure(ZoneId zone)
{
    const std::size_t erased = std::erase_if(
        Failures_,
        [zone](const ZoneLoadFailure& failure) { return failure.Zone == zone; });
    return erased > 0;
}

void AsyncZoneLoader::RemoveInFlight(ZoneId zone)
{
    std::erase_if(
        InFlight,
        [zone](const InFlightLoad& load) { return load.Zone == zone; });
}
