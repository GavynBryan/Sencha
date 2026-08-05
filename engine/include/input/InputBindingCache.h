#pragma once

#include <assets/data/DataAssetCache.h>
#include <input/InputActionRegistry.h>
#include <input/InputBindings.h>
#include <input/InputProfileData.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

//=============================================================================
// InputBindingCache
//
// Joins an authored profile to its action set and compiles the pair into the
// flat tables a resolve pass walks: dense action indices, contexts sorted by
// claim order, every name already resolved.
//
// Rebinding is driven by the data cache's reload version, the same way movement
// profiles rebind. A profile whose bindings fail to resolve keeps its previous
// tables and reports the error once, so a bad hot-reload cannot silently leave
// the player without controls.
//=============================================================================

struct InputProfileHandle
{
    DataAssetHandle Value;

    [[nodiscard]] bool IsValid() const { return Value.IsValid(); }
};

class InputBindingCache
{
public:
    explicit InputBindingCache(DataAssetCache& dataAssets);

    // The compiled tables for a profile, or null when nothing could be bound at
    // all. Bindings that fail individually are dropped and the rest still bind,
    // so one bad entry costs its own controls rather than every control.
    //
    // `newError` receives the failures the first time they appear, so a caller
    // can log them without repeating every frame.
    [[nodiscard]] const BoundInputProfile* Get(InputProfileHandle handle,
                                               std::string* newError = nullptr);

    // The action registry a profile's action set produced. Consumers resolve
    // their action names against this once and index by id afterwards.
    [[nodiscard]] const InputActionRegistry* GetActions(InputProfileHandle handle,
                                                        std::string* newError = nullptr);

    void Clear();
    [[nodiscard]] std::size_t Size() const { return Entries.size(); }
    [[nodiscard]] std::uint64_t RebuildCount() const { return Rebuilds; }

private:
    struct Entry
    {
        DataAssetCacheHandle ProfileLease;
        DataAssetCacheHandle ActionSetLease;
        InputActionRegistry Actions;
        BoundInputProfile Profile;
        std::uint64_t ProfileVersion = 0;
        std::uint64_t ActionSetVersion = 0;
        bool Bound = false;
        // Every binding that did not resolve, not just the first: fixing one
        // should not be how the author discovers the next.
        std::vector<std::string> Errors;
        bool ErrorDelivered = false;
    };

    Entry* Resolve(InputProfileHandle handle, std::string* newError);
    bool Rebuild(Entry& entry, const CompiledInputProfile& profile);

    DataAssetCache& DataAssets;
    std::unordered_map<std::uint64_t, Entry> Entries;
    std::uint64_t Rebuilds = 0;
};
