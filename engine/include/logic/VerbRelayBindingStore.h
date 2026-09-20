#pragma once

#include <assets/data/DataAssetCache.h>
#include <authored/VerbBindingSet.h>
#include <authored/VerbId.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class World;
struct VerbRelay;

//=============================================================================
// VerbRelayBindingStore
//
// Where a placed relay's binding asset becomes something this World can
// invoke. A World resource, because the resolved bindings carry this World's
// verb ids and entity handles and mean nothing in another.
//
// Derived state with explicit invalidation, the shape MovementProfileBindingCache
// has: one entry per binding asset, rebuilt when the asset's reload version
// moves or when a binding it compiled has gone stale against the catalog. A
// relay drain therefore costs a hash lookup and a revision comparison, never a
// schema traversal, and a hot-reloaded binding file takes effect at the next
// activation without anything polling it.
//=============================================================================
class VerbRelayBindingStore
{
public:
    explicit VerbRelayBindingStore(DataAssetCache& dataAssets);

    // The compiled binding a relay names, or null. Errors are reported once per
    // rebuild through `newErrors`, so a relay that fires every tick against a
    // broken asset does not bury every other diagnostic in the log.
    [[nodiscard]] const CompiledVerbBinding* Resolve(const VerbRelay& relay,
                                                     const World& world,
                                                     std::vector<std::string>* newErrors);

    void Clear();
    [[nodiscard]] std::size_t Size() const { return Entries.size(); }
    [[nodiscard]] std::uint64_t RebuildCount() const { return Rebuilds; }

private:
    struct Entry
    {
        DataAssetCacheHandle Lease;
        VerbBindingSet Bindings;
        std::uint64_t ReloadVersion = 0;
        VerbCatalogId Catalog;
        std::vector<std::string> Errors;
        bool ErrorsDelivered = false;
    };

    DataAssetCache& DataAssets;
    std::unordered_map<std::uint64_t, Entry> Entries;
    std::uint64_t Rebuilds = 0;
};
