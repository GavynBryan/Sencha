#pragma once

#include <core/logging/Logger.h>
#include <jobs/AsyncTaskQueue.h>
#include <zone/ZoneId.h>
#include <zone/ZoneLoadFailure.h>
#include <zone/ZoneLoadPackage.h>
#include <zone/ZoneParticipation.h>

#include <functional>
#include <memory>
#include <span>
#include <vector>

class AssetPreload;
class ComponentSerializerRegistry;
class RuntimeFrameLoop;
class RuntimeWorld;
struct RuntimeZoneRecord;
struct SceneSerializationContext;
class WorldComponentSchema;

//=============================================================================
// AsyncZoneLoader
//
// Loads zones without blocking the frame. BeginLoad submits an async task whose
// work stage builds detached plain CPU package data. The commit stage imports
// that package into a hidden RuntimeWorld partition, runs the owner-thread
// finalize callback, and publishes the zone atomically at the drain point.
//
// Build callbacks must not touch live Worlds, caches, services, or backend
// objects. Finalize runs while the imported partition is still hidden and
// returns false to cancel the whole import before publication.
//=============================================================================
class AsyncZoneLoader
{
public:
    using BuildFn = std::function<void(ZoneLoadPackage&)>;
    using FinalizeFn =
        std::function<bool(RuntimeWorld&, RuntimeZoneRecord&)>;

    AsyncZoneLoader(
        AsyncTaskQueue& tasks,
        RuntimeWorld& world,
        const WorldComponentSchema& schema,
        const ComponentSerializerRegistry& serializers,
        SceneSerializationContext& sceneContext,
        RuntimeFrameLoop& runtime);

    // Starts loading. The zone must be valid, not loaded/importing, and not in
    // flight. The package is born with the requested ZoneId.
    AsyncTaskHandle BeginLoad(
        ZoneId zone,
        BuildFn build,
        ZoneParticipation participation = {});

    AsyncTaskHandle BeginLoad(
        ZoneId zone,
        BuildFn build,
        FinalizeFn finalize,
        ZoneParticipation participation = {});

    // Asset-gated variant. If package preparation finishes first, publication
    // remains deferred until the preload completes. A failed/cancelled preload
    // counts as complete; owner-thread decoding may use synchronous fallback.
    AsyncTaskHandle BeginLoad(
        ZoneId zone,
        BuildFn build,
        FinalizeFn finalize,
        ZoneParticipation participation,
        std::shared_ptr<AssetPreload> assets);

    [[nodiscard]] bool IsLoading(ZoneId zone) const;

    // Best effort, like AsyncTaskQueue::Cancel: fails only while the build is
    // actively executing on the task thread.
    bool CancelLoad(ZoneId zone);

    // Requests final detach at the next commit drain. Destruction itself occurs
    // after the explicit residency visit, not inside this callback.
    AsyncTaskHandle RequestDestroy(ZoneId zone);

    // Loads that refused, newest last, one entry per zone. A caller that reissues
    // purely from demand would retry a broken zone every frame, so the streaming
    // policy consults these and diagnostics report them.
    [[nodiscard]] std::span<const ZoneLoadFailure> Failures() const
    {
        return { Failures_.data(), Failures_.size() };
    }
    [[nodiscard]] const ZoneLoadFailure* FindFailure(ZoneId zone) const;
    // True when the record existed. Call after the zone's content changes, so a
    // recook recovers without restarting the process.
    bool ClearFailure(ZoneId zone);
    void ClearFailures() { Failures_.clear(); }

private:
    struct InFlightLoad
    {
        ZoneId Zone;
        AsyncTaskHandle Handle;
        std::shared_ptr<AssetPreload> Assets;
    };

    void RemoveInFlight(ZoneId zone);
    void ImportAndFinalize(
        ZoneId zone,
        std::unique_ptr<ZoneLoadPackage> package,
        FinalizeFn& finalize,
        ZoneParticipation participation,
        const std::shared_ptr<AssetPreload>& assets);
    // Coalesces to one record per zone and logs once per recorded failure, not
    // once per attempt.
    void RecordFailure(ZoneId zone, ZoneLoadStage stage, std::string message);

    AsyncTaskQueue& Tasks;
    RuntimeWorld& RuntimeWorldState;
    const WorldComponentSchema& Schema;
    const ComponentSerializerRegistry& Serializers;
    SceneSerializationContext& SceneContext;
    RuntimeFrameLoop& Runtime;
    Logger Log;
    std::vector<InFlightLoad> InFlight;
    std::vector<ZoneLoadFailure> Failures_;
};
