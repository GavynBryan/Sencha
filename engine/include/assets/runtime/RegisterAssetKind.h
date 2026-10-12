#pragma once

#include <assets/runtime/AssetSystem.h>
#include <core/assets/AssetKindRegistry.h>
#include <core/assets/AssetLease.h>
#include <core/assets/AssetStore.h>

#include <cassert>
#include <functional>
#include <string_view>
#include <utility>

namespace AssetKindDetail
{
    // A loader whose commit resolves references to other assets takes the
    // front door to resolve them through; one with nothing to resolve does not.
    template<typename TLoader>
    auto CommitTyped(TLoader& loader, AssetStaging&& staged, AssetSystem& assets)
    {
        if constexpr (requires { loader.CommitTyped(std::move(staged), assets); })
            return loader.CommitTyped(std::move(staged), assets);
        else
            return loader.CommitTyped(std::move(staged));
    }

    template<typename TLoader>
    constexpr bool CanReload = requires(TLoader& loader, AssetStaging&& staged, AssetSystem& assets)
    {
        loader.CommitReload(std::move(staged), assets);
    } || requires(TLoader& loader, AssetStaging&& staged)
    {
        loader.CommitReload(std::move(staged));
    };

    template<typename TLoader>
    bool CommitReload(TLoader& loader, AssetStaging&& staged, AssetSystem& assets)
    {
        if constexpr (requires { loader.CommitReload(std::move(staged), assets); })
            return loader.CommitReload(std::move(staged), assets);
        else
            return loader.CommitReload(std::move(staged));
    }

    template<typename TLoader, typename TCache>
    AssetKindRegistration Make(AssetSystem& assets, AssetType type, TLoader& loader, TCache* cache,
                               IAssetListStore* listStore)
    {
        AssetKindRegistration kind = MakeBuiltinAssetKind(type);
        kind.Stager = &loader;
        if (cache == nullptr)
            return kind;
        kind.Store = cache;
        kind.ListStore = listStore;
        kind.Commit = [type, &loader, cache, &assets](AssetStaging&& staged) -> AssetCommitOutcome
        {
            auto committed = CommitTyped(loader, std::move(staged), assets);
            if constexpr (requires { committed.FailedDependency; })
            {
                if (!committed.IsValid())
                    return AssetCommitOutcome::DependencyFailed(std::move(committed.FailedDependency));
                return AssetLease::Adopt(type, *cache, committed.Handle.ToToken());
            }
            else
            {
                if (!committed.IsValid())
                    return {};
                return AssetLease::Adopt(type, *cache, committed.ToToken());
            }
        };
        if constexpr (CanReload<TLoader>)
        {
            kind.Reload = [&loader, &assets](AssetStaging&& staged)
            {
                return CommitReload(loader, std::move(staged), assets);
            };
        }
        return kind;
    }

    inline void Register(AssetSystem& assets, AssetKindRegistration kind)
    {
        const bool registered = assets.Kinds().Register(std::move(kind));
        assert(registered && "an asset kind registered twice or claims another's extension");
        (void)registered;
    }
}

// Attaches one of the engine's typed loader-and-cache pairs to the front door
// as the kind for `type`. The loader stages; the cache is the kind's store,
// and its typed CommitTyped / CommitReload become the kind's commit and reload.
//
// The stager is always wired because staging touches no cache; the commit
// half exists only for a cache this composition has, so a headless host still
// stages a mesh and reports the commit as a failure.
template<typename TLoader, typename TCache>
void RegisterAssetKind(AssetSystem& assets,
                       AssetType type,
                       TLoader& loader,
                       TCache* cache,
                       IAssetListStore* listStore = nullptr)
{
    AssetKindDetail::Register(assets, AssetKindDetail::Make(assets, type, loader, cache, listStore));
}

// The same, with what an asset of the kind that cannot be loaded resolves to.
// Only a kind that can reload takes one: the reload is what replaces it.
template<typename TLoader, typename TCache>
    requires AssetKindDetail::CanReload<TLoader>
void RegisterAssetKind(AssetSystem& assets,
                       AssetType type,
                       TLoader& loader,
                       TCache* cache,
                       IAssetListStore* listStore,
                       std::function<AssetLease(std::string_view)> standIn)
{
    AssetKindRegistration kind = AssetKindDetail::Make(assets, type, loader, cache, listStore);
    if (cache != nullptr)
        kind.StandIn = std::move(standIn);
    AssetKindDetail::Register(assets, std::move(kind));
}
