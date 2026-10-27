#include <runtime/spawn/NetPrefabSpawner.h>

#include <assets/runtime/AssetSystem.h>
#include <assets/scene/SceneCache.h>
#include <assets/scene/ScenePackageBuild.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <world/build/EntityBuildPackage.h>
#include <world/scene/SceneInstanceGroup.h>
#include <world/scene/SceneInstanceIndex.h>
#include <world/scene/SmapFormat.h>
#include <world/serialization/SceneSerializationContext.h>
#include <zone/ZonePackageImporter.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

NetPrefabSpawner::NetPrefabSpawner(const WorldComponentSchema& schema,
                                   const ComponentSerializerRegistry& serializers,
                                   LoggingProvider& logging)
    : Schema(schema)
    , Serializers(serializers)
    , Logging(logging)
{
}

NetPrefabSpawner::~NetPrefabSpawner() = default;

void NetPrefabSpawner::ConnectAssets(AssetSystem* assets, SceneCache* scenes)
{
    Resident.clear();
    Refused.clear();

    Assets = assets;
    Scenes = scenes;
    SceneContext = assets != nullptr
        ? std::make_unique<SceneSerializationContext>(Logging, assets)
        : nullptr;
}

void NetPrefabSpawner::RefuseOnce(AssetId scene, std::string_view reason)
{
    if (!Refused.insert(scene).second)
        return;
    Logging.GetLogger<NetPrefabSpawner>().Error(
        "NetPrefabSpawner: prefab {} will not spawn: {}. Entities the authority "
        "builds from it arrive with their state and no body.",
        AssetIdToString(scene), reason);
}

NetPrefabReadiness NetPrefabSpawner::Prepare(AssetId scene)
{
    if (!scene.IsValid())
        return NetPrefabReadiness::Unavailable;
    if (Resident.contains(scene))
        return NetPrefabReadiness::Ready;
    if (Assets == nullptr || Scenes == nullptr || SceneContext == nullptr)
    {
        RefuseOnce(scene, "this process has no asset system");
        return NetPrefabReadiness::Unavailable;
    }

    // Id first, exactly as an authored reference resolves: the id is what the
    // wire carries and the path is only how this machine finds the file. With
    // no fallback path, an id this build does not know resolves to nothing.
    const std::string path(
        Assets->ResolveRefPath(scene, std::string_view{}, AssetType::Scene));
    if (path.empty())
    {
        RefuseOnce(scene, "no scene asset in this build has that id");
        return NetPrefabReadiness::Unavailable;
    }

    // Held for the session: a prefab resolved once is resolved for every peer
    // that spawns one afterwards, which is what keeps a join from paying a
    // file read inside a snapshot.
    AssetLease lease = Assets->LoadLease(path, AssetType::Scene);
    if (!lease.IsValid())
    {
        RefuseOnce(scene, "'" + path + "' did not load");
        return NetPrefabReadiness::Unavailable;
    }

    const SmapContents* contents = Scenes->Get(SceneHandle::FromToken(lease.OpaqueToken()));
    const std::size_t roots = contents != nullptr ? CountRootRecords(*contents) : 0;
    if (roots != 1)
    {
        RefuseOnce(scene,
                   "'" + path + "' expands to " + std::to_string(roots)
                       + " root entities; a replicated spawn needs exactly one");
        return NetPrefabReadiness::Unavailable;
    }

    Resident.emplace(scene, ResidentPrefab{ path, std::move(lease) });
    return NetPrefabReadiness::Ready;
}

EntityId NetPrefabSpawner::Instantiate(AssetId scene,
                                       World& world,
                                       StoragePartitionId partition)
{
    const auto found = Resident.find(scene);
    if (found == Resident.end() || Assets == nullptr || Scenes == nullptr
        || SceneContext == nullptr)
        return EntityId{};

    const AssetRecord* record = Assets->Resolve(found->second.Path, AssetType::Scene);
    if (record == nullptr)
        return EntityId{};

    // Built per spawn: the scene is shared, the group identity is not, and it
    // is the group identity that makes one pawn's children distinguishable from
    // another's. The scene is already resident, so this is a decode of parsed
    // data rather than a file read.
    ScenePackageBuild build(*Assets, *Scenes, *record);
    build.Build(Serializers, SmapPackageOptions{ .StripPersistentIdentity = true });
    if (!build.Settle())
    {
        RefuseOnce(scene, build.Error());
        return EntityId{};
    }

    const SceneInstanceId instance{ SceneInstanceIdRuntimeBit | NextInstanceValue++ };
    std::unique_ptr<EntityBuildPackage> package = build.TakePackage();
    if (package == nullptr)
    {
        build.ReleaseScene();
        return EntityId{};
    }
    for (std::uint32_t i = 0; i < package->EntityCount(); ++i)
        (void)package->AddComponent(PackageEntityId{ i },
                                    SceneInstance{ scene, instance });

    std::vector<EntityId> created;
    ZoneImportError importError;
    const bool imported = ImportPackageIntoPartition(
        world, Schema, *package, partition, ZoneId{}, Serializers, *SceneContext,
        &importError, &created);
    build.ReleaseScene();

    if (!imported)
    {
        // The import destroyed whatever it made, so there is nothing here but
        // the report.
        Logging.GetLogger<NetPrefabSpawner>().Error(
            "NetPrefabSpawner: prefab '{}' failed to import: {}",
            found->second.Path, importError.Message);
        return EntityId{};
    }

    if (const EntityId root = SingleRoot(world, created); root.IsValid())
        return root;

    // Validated at Prepare, so reaching here means the package disagreed with
    // the contents it was built from.
    Logging.GetLogger<NetPrefabSpawner>().Error(
        "NetPrefabSpawner: prefab '{}' imported with no root entity",
        found->second.Path);
    for (const EntityId entity : created)
        if (world.IsAlive(entity))
            world.DestroyEntity(entity);
    return EntityId{};
}

void NetPrefabSpawner::Despawn(World& world, EntityId root)
{
    if (!root.IsValid() || !world.IsAlive(root))
        return;

    const SceneInstance* group = world.IsRegistered<SceneInstance>()
        ? world.TryGet<SceneInstance>(root)
        : nullptr;
    if (group == nullptr || world.TryGetResource<SceneInstanceIndex>() == nullptr)
    {
        world.DestroyEntity(root);
        return;
    }
    DestroySceneInstance(world, group->Id);
}
