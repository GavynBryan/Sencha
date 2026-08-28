#include <zone/AsyncZoneLoader.h>

#include <assets/runtime/AssetPreloader.h>
#include <assets/runtime/AssetSystem.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/WorldComponentSchema.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/RuntimeWorld.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/scene/SmapFormat.h>
#include <zone/ZonePackageImporter.h>

#include <algorithm>
#include <any>
#include <string>
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

    AsyncTaskHandle handle = Tasks.Submit<std::unique_ptr<EntityBuildPackage>>(
        // Work, on a task thread: package-local identity and owned CPU payloads
        // need no synchronization with the live entity world.
        [build = std::move(build)]() mutable {
            auto package = std::make_unique<EntityBuildPackage>();
            build(*package);
            return package;
        },
        // Commit, on the owner thread at the drain point.
        [this, zone, participation, finalize = std::move(finalize), assets](
            std::unique_ptr<EntityBuildPackage> package) mutable {
            CommitOrDefer(zone, std::move(package), std::move(finalize),
                          participation, std::move(assets));
        });

    InFlight.push_back(InFlightLoad{ zone, handle, std::move(assets) });
    return handle;
}

AsyncTaskHandle AsyncZoneLoader::BeginLoadScene(
    ZoneId zone,
    std::string_view sceneAssetPath,
    AssetSystem& assets,
    SceneStageFn stageExtra,
    SceneFinalizeFn finalize,
    ZoneParticipation participation,
    std::shared_ptr<AssetPreload> preload)
{
    assert(zone.IsValid() && "AsyncZoneLoader::BeginLoadScene: zone id must be valid");
    assert(RuntimeWorldState.FindZone(zone) == nullptr
           && "AsyncZoneLoader::BeginLoadScene: zone is already loaded or importing");
    assert(!IsLoading(zone)
           && "AsyncZoneLoader::BeginLoadScene: zone load is already in flight");

    const AssetRecord* record = assets.Resolve(sceneAssetPath, AssetType::Scene);
    if (record == nullptr)
    {
        RecordFailure(zone, ZoneLoadStage::Build,
                      "scene '" + std::string(sceneAssetPath)
                          + "' did not resolve to a cooked scene asset");
        if (preload)
            preload->ReleaseAll();
        return {};
    }

    // Owner thread: a resident scene skips the read and the parse. The shared
    // payload is captured here so the task thread never touches the cache.
    SceneHandle residentScene = assets.TryAcquireScene(record->Path);
    std::shared_ptr<const SmapContents> residentContents =
        residentScene.IsValid() ? assets.GetSceneContentsShared(residentScene)
                                : nullptr;

    struct SceneWork
    {
        std::unique_ptr<EntityBuildPackage> Package;
        AssetStaging Staging; // engaged only when the scene was not resident
        bool Staged = false;
        std::string Error;
    };

    AsyncTaskHandle handle = Tasks.Submit<std::unique_ptr<SceneWork>>(
        // Work, on a task thread: file IO, parse, and package build against
        // immutable inputs. `Serializers` sees only const reads; module
        // registration is sealed before zones stream.
        [this, record = *record, residentContents,
         stageExtra = std::move(stageExtra), &assets]() mutable {
            auto work = std::make_unique<SceneWork>();
            const SmapContents* contents = residentContents.get();
            if (contents == nullptr)
            {
                work->Staging = assets.StageScene(record);
                if (!work->Staging.IsValid())
                {
                    work->Error = std::move(work->Staging.Error);
                    return work;
                }
                work->Staged = true;
                contents = std::any_cast<SmapContents>(&work->Staging.Payload);
            }

            auto package = std::make_unique<EntityBuildPackage>();
            SmapError buildError;
            if (!BuildEntityPackageFromSmap(*contents, Serializers, *package,
                                            &buildError))
            {
                work->Error = std::move(buildError.Message);
                return work;
            }
            if (stageExtra)
                stageExtra(*contents);
            work->Package = std::move(package);
            return work;
        },
        // Commit, on the owner thread at the drain point: residency first,
        // then the ordinary import path.
        [this, zone, participation, residentScene, &assets,
         finalize = std::move(finalize),
         preload](std::unique_ptr<SceneWork> work) mutable {
            SceneHandle scene = residentScene;
            if (work->Error.empty() && work->Staged)
            {
                scene = assets.CommitScene(std::move(work->Staging));
                if (!scene.IsValid())
                    work->Error = "scene contents did not commit into the cache";
            }

            if (!work->Error.empty() || work->Package == nullptr)
            {
                RemoveInFlight(zone);
                RecordFailure(zone, ZoneLoadStage::Build,
                              work->Error.empty() ? "package was not produced"
                                                  : std::move(work->Error));
                if (scene.IsValid())
                    assets.ReleaseScene(scene);
                if (preload)
                    preload->ReleaseAll();
                return;
            }

            // The reference is scaffolding for this import; it releases on
            // the owner thread once publication settles, even when a pending
            // preload defers it. The payload itself is shared, so a finalize
            // that runs after an unrelated release still reads valid data.
            std::shared_ptr<const SmapContents> contents =
                assets.GetSceneContentsShared(scene);
            std::shared_ptr<void> sceneLease(
                nullptr, [scene, &assets](void*) { assets.ReleaseScene(scene); });

            FinalizeFn wrapped;
            if (finalize)
                wrapped = [finalize = std::move(finalize), contents](
                              RuntimeWorld& world, RuntimeZoneRecord& zoneRecord) {
                    return finalize(world, zoneRecord, *contents);
                };

            CommitOrDefer(zone, std::move(work->Package), std::move(wrapped),
                          participation, std::move(preload),
                          std::move(sceneLease));
        });

    InFlight.push_back(InFlightLoad{ zone, handle, std::move(preload) });
    return handle;
}

void AsyncZoneLoader::CommitOrDefer(
    ZoneId zone,
    std::unique_ptr<EntityBuildPackage> package,
    FinalizeFn finalize,
    ZoneParticipation participation,
    std::shared_ptr<AssetPreload> assets,
    std::shared_ptr<void> keepAlive)
{
    // A cancelled preload counts as complete; owner-thread decoding may use
    // its synchronous fallback.
    if (assets && !assets->IsComplete() && !assets->IsCancelled())
    {
        auto deferredPackage =
            std::make_shared<std::unique_ptr<EntityBuildPackage>>(
                std::move(package));
        auto deferredFinalize = std::make_shared<FinalizeFn>(std::move(finalize));
        assets->SetOnComplete(
            [this, zone, participation, deferredPackage, deferredFinalize,
             assets, keepAlive = std::move(keepAlive)]() mutable {
                ImportAndFinalize(zone, std::move(*deferredPackage),
                                  *deferredFinalize, participation, assets);
            });
        return;
    }

    ImportAndFinalize(zone, std::move(package), finalize, participation, assets);
}

void AsyncZoneLoader::ImportAndFinalize(
    ZoneId zone,
    std::unique_ptr<EntityBuildPackage> package,
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
            zone,
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
