#include <core/assets/AssetStoreTable.h>

#include <cstddef>

namespace
{
    std::size_t Index(AssetType type) { return static_cast<std::size_t>(type); }
    std::size_t Index(AssetArity arity) { return static_cast<std::size_t>(arity); }
}

void AssetStoreTable::Add(AssetType type, AssetArity arity, IAssetStore& store)
{
    std::vector<IAssetStore*>& stores = StoresByArity[Index(arity)];
    if (stores.size() <= Index(type))
        stores.resize(Index(type) + 1, nullptr);
    stores[Index(type)] = &store;
}

IAssetStore* AssetStoreTable::Find(AssetType type, AssetArity arity) const
{
    const std::vector<IAssetStore*>& stores = StoresByArity[Index(arity)];
    return Index(type) < stores.size() ? stores[Index(type)] : nullptr;
}
