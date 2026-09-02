#pragma once

#include <assets/runtime/AssetSystem.h>
#include <core/assets/AssetKindRegistry.h>
#include <core/assets/AssetLease.h>
#include <core/assets/AssetStore.h>

#include <cassert>
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
    AssetKindRegistration kind = MakeBuiltinAssetKind(type);
    kind.Stager = &loader;
    if (cache != nullptr)
    {
        kind.Store = cache;
        kind.ListStore = listStore;
        kind.Commit = [type, &loader, cache, &assets](AssetStaging&& staged) -> AssetLease
        {
            const auto handle = AssetKindDetail::CommitTyped(loader, std::move(staged), assets);
            if (!handle.IsValid())
                return {};
            return AssetLease::Adopt(type, *cache, handle.ToToken());
        };
        if constexpr (AssetKindDetail::CanReload<TLoader>)
        {
            kind.Reload = [&loader, &assets](AssetStaging&& staged)
            {
                return AssetKindDetail::CommitReload(loader, std::move(staged), assets);
            };
        }
    }

    const bool registered = assets.Kinds().Register(std::move(kind));
    assert(registered && "an asset kind registered twice or claims another's extension");
    (void)registered;
}
