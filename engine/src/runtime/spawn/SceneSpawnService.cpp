#include <runtime/spawn/SceneSpawnService.h>

#include <assets/runtime/AssetSystem.h>
#include <assets/scene/SceneCache.h>
#include <assets/scene/ScenePackageBuild.h>
#include <core/logging/LoggingProvider.h>
#include <world/RuntimeWorld.h>
#include <world/build/EntityBuildPackage.h>
#include <world/scene/SceneInstance.h>
#include <world/scene/SceneInstanceGroup.h>
#include <world/scene/SceneInstanceIndex.h>
#include <world/scene/SmapFormat.h>
#include <world/transform/TransformComponents.h>
#include <zone/ZonePackageImporter.h>

#include <cassert>
#include <string>
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
    SceneSpawnLifetime Lifetime = SceneSpawnLifetime::Independent;
    EntityId RootEntity;
    Phase State = Phase::Staging;
    // Ended before it was published; the pump discards rather than instantiates.
    bool Withdrawn = false;
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

void SceneSpawnService::ConnectAssets(AssetSystem* assets, SceneCache* scenes)
{
    if (assets == nullptr)
    {
        for (const auto& request : Requests)
            if (request->State == Request::Phase::Staging
                || request->State == Request::Phase::Ready)
                Discard(*request);
        FirstUnsettled = Requests.size();
    }
    Assets = assets;
    Scenes = scenes;
    SceneContext = assets != nullptr
        ? std::make_unique<SceneSerializationContext>(Logging, assets)
        : nullptr;
}

SceneSpawnService::Request* SceneSpawnService::FindRequest(SceneSpawnId id) const
{
    // Ids mint sequentially from 1 and every RequestSpawn appends exactly one
    // record before any early return, so the id names its deque slot.
    if (!id.IsValid() || id.Value >= NextSpawnValue)
        return nullptr;
    return Requests[static_cast<std::size_t>(id.Value - 1)].get();
}

SceneSpawnId SceneSpawnService::RequestSpawn(std::string_view sceneAssetPath,
                                             const Transform3f& root,
                                             StoragePartitionId partition,
                                             SceneSpawnLifetime lifetime)
{
    auto request = std::make_unique<Request>();
    request->Id = SceneSpawnId{ NextSpawnValue++ };
    request->Instance = MintRuntimeSceneInstanceId();
    request->ScenePath = std::string(sceneAssetPath);
    request->Root = root;
    request->Partition = partition;
    request->Lifetime = lifetime;

    Request* record = request.get();
    Requests.push_back(std::move(request));

    Logger& log = Logging.GetLogger<SceneSpawnService>();
    if (Assets == nullptr || Scenes == nullptr)
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
        log.Error("SceneSpawnService: spawn refused: {}", record->Error);
        return record->Id;
    }
    record->Source = asset->Id;

    // Shared rather than unique because the task closures must stay copyable;
    // each phase still runs on exactly the thread its name says.
    auto build = std::make_shared<ScenePackageBuild>(*Assets, *Scenes, *asset);
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
            Request* pending = FindRequest(id);
            if (pending == nullptr)
                return;
            if (!build->Settle())
            {
                pending->State = Request::Phase::Failed;
                pending->Error = build->Error();
                Logging.GetLogger<SceneSpawnService>().Error(
                    "SceneSpawnService: spawn of '{}' failed: {}",
                    pending->ScenePath, pending->Error);
                return;
            }
            if (pending->Lifetime == SceneSpawnLifetime::OwnedByRoot)
            {
                const SmapContents* contents = build->Contents();
                const std::size_t roots =
                    contents != nullptr ? CountRootRecords(*contents) : 0;
                if (roots != 1)
                {
                    build->ReleaseScene();
                    pending->State = Request::Phase::Failed;
                    pending->Error = "a root-owned spawn needs one root entity; '"
                        + pending->ScenePath + "' has " + std::to_string(roots);
                    Logging.GetLogger<SceneSpawnService>().Error(
                        "SceneSpawnService: spawn refused: {}", pending->Error);
                    return;
                }
            }
            pending->Build = build;
            pending->Package = build->TakePackage();
            pending->State = Request::Phase::Ready;
        });

    return record->Id;
}

bool SceneSpawnService::RequestDespawn(SceneSpawnId id)
{
    Request* request = FindRequest(id);
    if (request == nullptr)
        return false;
    switch (request->State)
    {
    case Request::Phase::Live:
        request->State = Request::Phase::DespawnQueued;
        PendingDespawns.push_back(id);
        return true;
    case Request::Phase::Staging:
    case Request::Phase::Ready:
        // Withdrawn before anything exists. A staging request finishes its
        // task -- the task cannot be recalled -- and the pump discards what it
        // built instead of publishing it.
        if (request->Withdrawn)
            return false;
        request->Withdrawn = true;
        return true;
    default:
        return false;
    }
}

bool SceneSpawnService::IsDespawnRequested(SceneSpawnId id) const
{
    const Request* request = FindRequest(id);
    return request != nullptr
        && (request->Withdrawn
            || request->State == Request::Phase::DespawnQueued
            || request->State == Request::Phase::Despawned);
}

SceneSpawnStatus SceneSpawnService::Status(SceneSpawnId id) const
{
    const Request* request = FindRequest(id);
    if (request == nullptr)
        return SceneSpawnStatus::Unknown;
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
    return SceneSpawnStatus::Unknown;
}

EntityId SceneSpawnService::Root(SceneSpawnId id) const
{
    const Request* request = FindRequest(id);
    if (request == nullptr
        || (request->State != Request::Phase::Live
            && request->State != Request::Phase::DespawnQueued)
        || !WorldState.Entities().IsAlive(request->RootEntity))
        return EntityId{};
    return request->RootEntity;
}

std::span<const EntityId> SceneSpawnService::Entities(SceneSpawnId id) const
{
    const Request* request = FindRequest(id);
    if (request == nullptr)
        return {};
    if (const auto* index =
            WorldState.Entities().TryGetResource<SceneInstanceIndex>())
        return index->Entities(request->Instance);
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
        World& world = WorldState.Entities();
        if (const auto* index = world.TryGetResource<SceneInstanceIndex>())
        {
            for (EntityId entity : index->Entities(request.Instance))
            {
                if (world.TryGet<Parent>(entity) != nullptr)
                    continue;
                if (auto* local = world.TryGet<LocalTransform>(entity))
                    local->Value = request.Root * local->Value;
            }
        }
        request.RootEntity = SingleRoot(world, Entities(request.Id));
        request.State = Request::Phase::Live;

        if (request.Lifetime == SceneSpawnLifetime::OwnedByRoot)
        {
            // Settlement admitted exactly one root record, and the importer
            // wires every package parent; no single root means that broke.
            assert(request.RootEntity.IsValid());
            if (request.RootEntity.IsValid())
            {
                RootOwned.push_back(request.Id);
            }
            else
            {
                DestroySceneInstance(world, request.Instance);
                request.State = Request::Phase::Failed;
                request.Error = "a root-owned spawn imported without a single root";
                log.Error("SceneSpawnService: spawn of '{}' failed: {}",
                          request.ScenePath, request.Error);
            }
        }
    }

    // The parse was scaffolding either way; the entities are the product.
    if (request.Build != nullptr)
    {
        request.Build->ReleaseScene();
        request.Build.reset();
    }
    // Nothing logs the path after this point, and settled records are kept
    // for the session; a long-running game should not hoard spawn strings.
    request.ScenePath = std::string();
}

// A withdrawn request reaching the pump: what was built is let go of without
// touching the world, and the request ends as if its group had been destroyed
// -- which, to its owner, is what happened.
void SceneSpawnService::Discard(Request& request)
{
    request.Package.reset();
    if (request.Build != nullptr)
    {
        request.Build->ReleaseScene();
        request.Build.reset();
    }
    request.ScenePath = std::string();
    request.State = Request::Phase::Despawned;
}

void SceneSpawnService::Pump()
{
    // Publication in request order: walk from the first request that could
    // still change and publish every consecutive settled-or-ready one,
    // stopping at the first still staging so a later completion can never
    // leapfrog an earlier request. The cursor then skips the settled prefix
    // forever after -- the walk costs what is in flight, not the session's
    // whole spawn history.
    for (std::size_t i = FirstUnsettled; i < Requests.size(); ++i)
    {
        Request& request = *Requests[i];
        if (request.State == Request::Phase::Staging)
            break;
        if (request.State == Request::Phase::Ready)
        {
            if (request.Withdrawn)
                Discard(request);
            else
                Instantiate(request);
        }
        if (i == FirstUnsettled)
            ++FirstUnsettled;
    }

    QueueDespawnsOfDeadRoots();

    // Despawns need no ordering: the group either exists or it does not.
    for (const SceneSpawnId id : PendingDespawns)
    {
        Request* request = FindRequest(id);
        if (request == nullptr || request->State != Request::Phase::DespawnQueued)
            continue;
        DestroySceneInstance(WorldState.Entities(), request->Instance);
        request->State = Request::Phase::Despawned;
    }
    PendingDespawns.clear();
}

void SceneSpawnService::QueueDespawnsOfDeadRoots()
{
    const World& world = WorldState.Entities();
    std::erase_if(RootOwned, [&](SceneSpawnId id) {
        Request* request = FindRequest(id);
        if (request->State != Request::Phase::Live)
            return true;
        if (world.IsAlive(request->RootEntity))
            return false;
        request->State = Request::Phase::DespawnQueued;
        PendingDespawns.push_back(id);
        return true;
    });
}
