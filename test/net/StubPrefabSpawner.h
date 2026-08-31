#pragma once

#include <net/NetSpawnPrefab.h>

#include <ecs/EntityId.h>
#include <ecs/StoragePartitionId.h>
#include <ecs/World.h>

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

//=============================================================================
// StubPrefabSpawner
//
// What a replication test needs from the prefab seam, without the content
// stack behind the real one: a prefab is a lambda that builds an entity, and
// an id nobody registered is a prefab this build does not have.
//
// The group is real, though. A prefab that builds children is what makes a
// destroy have to sweep more than the root, and pretending otherwise would
// leave the one case worth testing untested.
//=============================================================================
class StubPrefabSpawner final : public INetPrefabSpawner
{
public:
    // The root the spawn binds to, plus anything else the prefab creates.
    using Build = std::function<std::vector<EntityId>(World&, StoragePartitionId)>;

    void Register(AssetId scene, Build build)
    {
        Builds.emplace(scene, std::move(build));
    }

    // A prefab whose content is known but not yet loadable, which is what a
    // client sees on the snapshot before its scene finished arriving.
    void Withhold(AssetId scene) { Withheld.insert(scene); }
    void Release(AssetId scene) { Withheld.erase(scene); }

    [[nodiscard]] NetPrefabReadiness Prepare(AssetId scene) override
    {
        ++Prepares;
        return Builds.contains(scene) && !Withheld.contains(scene)
            ? NetPrefabReadiness::Ready
            : NetPrefabReadiness::Unavailable;
    }

    [[nodiscard]] EntityId Instantiate(AssetId scene,
                                       World& world,
                                       StoragePartitionId partition) override
    {
        const auto found = Builds.find(scene);
        if (found == Builds.end())
            return EntityId{};
        std::vector<EntityId> made = found->second(world, partition);
        if (made.empty())
            return EntityId{};
        Groups.emplace(Key(made.front()), made);
        return made.front();
    }

    void Despawn(World& world, EntityId root) override
    {
        const auto found = Groups.find(Key(root));
        if (found == Groups.end())
        {
            if (world.IsAlive(root))
                world.DestroyEntity(root);
            return;
        }
        for (const EntityId entity : found->second)
            if (world.IsAlive(entity))
                world.DestroyEntity(entity);
        Groups.erase(found);
    }

    // How many times the applier asked whether a prefab was buildable, so a
    // test can tell a deferral from a spawn that never looked.
    std::size_t Prepares = 0;

private:
    // EntityId is not hashable; the pair of numbers behind it is.
    [[nodiscard]] static std::uint64_t Key(EntityId entity)
    {
        return (static_cast<std::uint64_t>(entity.Generation) << 32)
             | static_cast<std::uint64_t>(entity.Index);
    }

    std::unordered_map<AssetId, Build> Builds;
    std::unordered_set<AssetId> Withheld;
    std::unordered_map<std::uint64_t, std::vector<EntityId>> Groups;
};
