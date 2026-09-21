#pragma once

#include <assets/data/DataAssetCache.h>
#include <authored/VerbBindingSet.h>
#include <authored/VerbId.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class AssetRegistry;
class World;
struct VerbRelay;

//=============================================================================
// VerbRelayBindingStore
//
// Where a placed relay's binding asset becomes something this World can
// invoke. A World resource, because the resolved bindings carry this World's
// verb ids and entity handles and mean nothing in another.
//
// Derived state with explicit invalidation: one VerbBindingSet per binding
// asset, which owns its own rebuild -- asset reload, catalog change, new
// declarations, new tags. A relay drain therefore costs a hash lookup and a
// few version comparisons, never a schema traversal, and a hot-reloaded
// binding file or a late-declared verb takes effect at the next activation
// without anything polling it.
//=============================================================================
class VerbRelayBindingStore
{
public:
    VerbRelayBindingStore(const AssetRegistry& assets, DataAssetCache& dataAssets);

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
        bool Built = false;
        std::vector<std::string> Errors;
        bool ErrorsDelivered = false;
    };

    const AssetRegistry& Assets;
    DataAssetCache& DataAssets;
    std::unordered_map<std::uint64_t, Entry> Entries;
    std::uint64_t Rebuilds = 0;
};
