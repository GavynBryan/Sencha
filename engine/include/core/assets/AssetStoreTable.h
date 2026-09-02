#pragma once

#include <core/assets/AssetRef.h>
#include <core/assets/AssetStore.h>

#include <array>
#include <vector>

//=============================================================================
// AssetStoreTable
//
// Which store holds a reference of each (kind, arity). Component lifetime
// retains and releases asset fields through this, so it names no concrete
// cache and a kind a game module registers is owned the same way as a
// built-in one.
//
// A Single reference lives in its kind's store. A Material list is a
// composite the MaterialSetCache represents, keyed as (Material, List) rather
// than passed off as the material store.
//
// A value that owns nothing: the host that owns the caches builds it and
// installs it on a World as a resource, and detaches it with an empty table
// before the caches go away.
//=============================================================================
class AssetStoreTable
{
public:
    void Add(AssetType type, AssetArity arity, IAssetStore& store);
    [[nodiscard]] IAssetStore* Find(AssetType type, AssetArity arity) const;

private:
    std::array<std::vector<IAssetStore*>, 2> StoresByArity;
};
