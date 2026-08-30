#include <runtime/spawn/NetPrefabSpawner.h>

#include <assets/runtime/AssetSystem.h>
#include <assets/scene/ScenePackageBuild.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <world/RuntimeWorld.h>
#include <world/build/EntityBuildPackage.h>
#include <world/scene/SceneInstanceIndex.h>
#include <world/scene/SmapFormat.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/transform/TransformComponents.h>
#include <zone/ZonePackageImporter.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    // A prefab expands to one group with one root. More than one parentless
    // entity has no root to bind the wire identity to, and zero has nothing to
    // bind at all -- either way the authority is describing something this
    // build cannot represent as a single replicated entity.
    std::size_t CountRoots(const SmapContents& contents)
    {
        std::size_t roots = 0;
        for (const SmapEntityRecord& entity : contents.Entities)
            if (entity.Parent == UINT32_MAX)
                ++roots;
        return roots;
    }
}

NetPrefabSpawner::NetPrefabSpawner(RuntimeWorld& world,
                                   const WorldComponentSchema& schema,
                                   const ComponentSerializerRegistry& serializers,
                                   LoggingProvider& logging)
    : WorldState(world)
    , Schema(schema)
    , Serializers(serializers)
    , Logging(logging)
{
}

NetPrefabSpawner::~NetPrefabSpawner()
{
    if (Assets == nullptr)
        return;
    for (auto& [id, prefab] : Resident)
        Assets->ReleaseScene(prefab.Scene);
}

void NetPrefabSpawner::ConnectAssets(AssetSystem* assets)
{
    if (Assets != nullptr)
    {
        for (auto& [id, prefab] : Resident)
            Assets->ReleaseScene(prefab.Scene);
    }
    Resident.clear();
    Refused.clear();

    Assets = assets;
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
    if (Assets == nullptr || SceneContext == nullptr)
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
    const SceneHandle handle = Assets->LoadScene(path);
    if (!handle.IsValid())
    {
        RefuseOnce(scene, "'" + path + "' did not load");
        return NetPrefabReadiness::Unavailable;
    }

    const SmapContents* contents = Assets->GetSceneContents(handle);
    const std::size_t roots = contents != nullptr ? CountRoots(*contents) : 0;
    if (roots != 1)
    {
        Assets->ReleaseScene(handle);
        RefuseOnce(scene,
                   "'" + path + "' expands to " + std::to_string(roots)
                       + " root entities; a replicated spawn needs exactly one");
        return NetPrefabReadiness::Unavailable;
    }

    Resident.emplace(scene, ResidentPrefab{ path, handle });
    return NetPrefabReadiness::Ready;
}

EntityId NetPrefabSpawner::Instantiate(AssetId scene,
                                       World& world,
                                       StoragePartitionId partition)
{
    const auto found = Resident.find(scene);
    if (found == Resident.end() || Assets == nullptr || SceneContext == nullptr)
        return EntityId{};

    const AssetRecord* record = Assets->Resolve(found->second.Path, AssetType::Scene);
    if (record == nullptr)
        return EntityId{};

    // Built per spawn: the scene is shared, the group identity is not, and it
    // is the group identity that makes one pawn's children distinguishable from
    // another's. The scene is already resident, so this is a decode of parsed
    // data rather than a file read.
    ScenePackageBuild build(*Assets, *record);
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

    for (const EntityId entity : created)
        if (world.TryGet<Parent>(entity) == nullptr)
            return entity;

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
    const auto* index = world.TryGetResource<SceneInstanceIndex>();
    if (group == nullptr || index == nullptr)
    {
        world.DestroyEntity(root);
        return;
    }

    // From a copy: destruction mutates the index through the component's own
    // hooks, so iterating the live span would walk what is being emptied.
    const std::span<const EntityId> members = index->Entities(group->Id);
    const std::vector<EntityId> snapshot(members.begin(), members.end());
    for (const EntityId entity : snapshot)
        if (world.IsAlive(entity))
            world.DestroyEntity(entity);
}
