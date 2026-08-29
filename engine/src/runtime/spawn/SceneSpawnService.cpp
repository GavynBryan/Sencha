#include <runtime/spawn/SceneSpawnService.h>

#include <assets/runtime/AssetSystem.h>
#include <assets/scene/SceneCache.h>
#include <assets/scene/ScenePackageBuild.h>
#include <core/logging/LoggingProvider.h>
#include <world/RuntimeWorld.h>
#include <world/build/EntityBuildPackage.h>
#include <world/scene/SceneInstance.h>
#include <world/scene/SceneInstanceIndex.h>
#include <world/scene/SmapFormat.h>
#include <world/transform/TransformComponents.h>
#include <zone/ZonePackageImporter.h>

#include <cassert>
#include <utility>

const char* SceneSpawnStatusName(SceneSpawnStatus status)
{
    switch (status)
    {
    case SceneSpawnStatus::Unknown:   return "unknown";
    case SceneSpawnStatus::Pending:   return "pending";
    case SceneSpawnStatus::Live:      return "live";
    case SceneSpawnStatus::Failed:    return "failed";
    case SceneSpawnStatus::Despawned: return "despawned";
    }
    return "unknown";
}

// One spawn from request to settlement. Phase is the internal lifecycle;
// Status() collapses it to the public vocabulary.
struct SceneSpawnService::Request
{
    enum class Phase : std::uint8_t
    {
        Staging,       // worker parse/build in flight, or queued behind it
        Ready,         // package built; waiting its turn at the pump
        Live,          // entities published
        Failed,        // refused at any stage; nothing was created
        DespawnQueued, // live, with destruction ordered for the next pump
        Despawned,     // entities destroyed
    };

    SceneSpawnId Id;
    SceneInstanceId Instance;
    std::string ScenePath;
    Transform3f Root;
    StoragePartitionId Partition;
    Phase State = Phase::Staging;
    std::string Error;

    // Worker product, consumed at instantiation.
    std::unique_ptr<EntityBuildPackage> Package;
    // The settled build, holding the scene's residency reference from request
    // until publication settles.
    std::shared_ptr<ScenePackageBuild> Build;
    AssetId Source;
};

SceneSpawnService::SceneSpawnService(RuntimeWorld& world,
                                     const WorldComponentSchema& schema,
                                     const ComponentSerializerRegistry& serializers,
                                     AsyncTaskQueue& tasks,
                                     LoggingProvider& logging)
    : WorldState(world)
    , Schema(schema)
    , Serializers(serializers)
    , Tasks(tasks)
    , Logging(logging)
{
}

SceneSpawnService::~SceneSpawnService() = default;

void SceneSpawnService::ConnectAssets(AssetSystem* assets)
{
    Assets = assets;
    SceneContext = assets != nullptr
        ? std::make_unique<SceneSerializationContext>(Logging, assets)
        : nullptr;
}

SceneSpawnId SceneSpawnService::RequestSpawn(std::string_view sceneAssetPath,
                                             const Transform3f& root,
                                             StoragePartitionId partition)
{
    auto request = std::make_unique<Request>();
    request->Id = SceneSpawnId{ NextSpawnValue++ };
    request->Instance =
        SceneInstanceId{ SceneInstanceIdRuntimeBit | NextInstanceValue++ };
    request->ScenePath = std::string(sceneAssetPath);
    request->Root = root;
    request->Partition = partition;

    Request* record = request.get();
    Requests.push_back(std::move(request));

    Logger& log = Logging.GetLogger<SceneSpawnService>();
    if (Assets == nullptr)
    {
        record->State = Request::Phase::Failed;
        record->Error = "no asset system is connected";
        log.Error("SceneSpawnService: spawn of '{}' refused: {}",
                  record->ScenePath, record->Error);
        return record->Id;
    }

    const AssetRecord* asset = Assets->Resolve(sceneAssetPath, AssetType::Scene);
    if (asset == nullptr)
    {
        record->State = Request::Phase::Failed;
        record->Error = "'" + record->ScenePath
            + "' did not resolve to a cooked scene asset";
        return record->Id;
    }
    record->Source = asset->Id;

    // Shared rather than unique because the task closures must stay copyable;
    // each phase still runs on exactly the thread its name says.
    auto build = std::make_shared<ScenePackageBuild>(*Assets, *asset);
    const SceneInstanceId instance = record->Instance;
    const AssetId source = record->Source;
    (void)Tasks.Submit<int>(
        // Work, on a task thread: parse and package build against immutable
        // inputs; serializer registration is sealed before spawns run.
        [this, build, instance, source]() {
            build->Build(Serializers,
                         SmapPackageOptions{ .StripPersistentIdentity = true });

            // Every spawned entity carries its group identity; the index
            // hooks make the spawn addressable the moment it imports.
            if (EntityBuildPackage* package = build->Package())
                for (std::uint32_t i = 0; i < package->EntityCount(); ++i)
                    (void)package->AddComponent(
                        PackageEntityId{ i }, SceneInstance{ source, instance });
            return 0;
        },
        // Commit, on the owner thread at the drain: record the product; the
        // pump publishes in request order.
        [this, build, id = record->Id](int) {
            for (auto& pending : Requests)
            {
                if (pending->Id != id)
                    continue;
                if (!build->Settle())
                {
                    pending->State = Request::Phase::Failed;
                    pending->Error = build->Error();
                    Logging.GetLogger<SceneSpawnService>().Error(
                        "SceneSpawnService: spawn of '{}' failed: {}",
                        pending->ScenePath, pending->Error);
                    return;
                }
                pending->Build = build;
                pending->Package = build->TakePackage();
                pending->State = Request::Phase::Ready;
                return;
            }
        });

    return record->Id;
}

bool SceneSpawnService::RequestDespawn(SceneSpawnId id)
{
    for (auto& request : Requests)
    {
        if (request->Id != id)
            continue;
        if (request->State != Request::Phase::Live)
            return false;
        request->State = Request::Phase::DespawnQueued;
        return true;
    }
    return false;
}

SceneSpawnStatus SceneSpawnService::Status(SceneSpawnId id) const
{
    for (const auto& request : Requests)
    {
        if (request->Id != id)
            continue;
        switch (request->State)
        {
        case Request::Phase::Staging:
        case Request::Phase::Ready:
            return SceneSpawnStatus::Pending;
        case Request::Phase::Live:
        case Request::Phase::DespawnQueued:
            return SceneSpawnStatus::Live;
        case Request::Phase::Failed:
            return SceneSpawnStatus::Failed;
        case Request::Phase::Despawned:
            return SceneSpawnStatus::Despawned;
        }
    }
    return SceneSpawnStatus::Unknown;
}

std::span<const EntityId> SceneSpawnService::Entities(SceneSpawnId id) const
{
    for (const auto& request : Requests)
    {
        if (request->Id != id)
            continue;
        if (const auto* index =
                WorldState.Entities().TryGetResource<SceneInstanceIndex>())
            return index->Entities(request->Instance);
        return {};
    }
    return {};
}

void SceneSpawnService::Instantiate(Request& request)
{
    assert(request.Package != nullptr);
    Logger& log = Logging.GetLogger<SceneSpawnService>();

    ZoneImportError importError;
    const bool imported = SceneContext != nullptr
        && ImportPackageIntoPartition(WorldState.Entities(), Schema, *request.Package,
                                      request.Partition, ZoneId{}, Serializers,
                                      *SceneContext, &importError);
    request.Package.reset();

    if (!imported)
    {
        request.State = Request::Phase::Failed;
        request.Error = importError.Message.empty()
            ? "no serialization context (asset system disconnected)"
            : std::move(importError.Message);
        log.Error("SceneSpawnService: spawn of '{}' failed to import: {}",
                  request.ScenePath, request.Error);
    }
    else
    {
        // Compose the placement onto the scene's roots; frame propagation
        // derives the world transforms from here.
        if (const auto* index =
                WorldState.Entities().TryGetResource<SceneInstanceIndex>())
        {
            World& world = WorldState.Entities();
            for (EntityId entity : index->Entities(request.Instance))
            {
                if (world.TryGet<Parent>(entity) != nullptr)
                    continue;
                if (auto* local = world.TryGet<LocalTransform>(entity))
                    local->Value = request.Root * local->Value;
            }
        }
        request.State = Request::Phase::Live;
    }

    // The parse was scaffolding either way; the entities are the product.
    if (request.Build != nullptr)
    {
        request.Build->ReleaseScene();
        request.Build.reset();
    }
}

void SceneSpawnService::Pump()
{
    // Publication in request order: walk from the front and publish every
    // consecutive settled-or-ready request, stopping at the first one still
    // staging so a later completion can never leapfrog an earlier request.
    for (auto& request : Requests)
    {
        if (request->State == Request::Phase::Staging)
            break;
        if (request->State == Request::Phase::Ready)
            Instantiate(*request);
    }

    // Despawns need no ordering: the group either exists or it does not.
    for (auto& request : Requests)
    {
        if (request->State != Request::Phase::DespawnQueued)
            continue;
        if (const auto* index =
                WorldState.Entities().TryGetResource<SceneInstanceIndex>())
        {
            const std::span<const EntityId> members =
                index->Entities(request->Instance);
            // Destruction mutates the index through the component hooks, so
            // destroy from a snapshot rather than the live span.
            const std::vector<EntityId> snapshot(members.begin(), members.end());
            for (EntityId entity : snapshot)
                WorldState.Entities().DestroyEntity(entity);
        }
        request->State = Request::Phase::Despawned;
    }
}
