#pragma once

#include <assets/data/DataAssetCache.h>
#include <input/InputActionRegistry.h>
#include <input/InputBindings.h>
#include <input/InputProfileData.h>

#include <cstdint>
#include <span>
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
// profiles rebind. A rebind that produces no tables at all keeps the previous
// ones, so a bad hot-reload cannot leave the player without controls while the
// bad edit is on disk.
//
// Action ids survive a rebind: the registry keeps each name's id, so a game
// that resolved its names at startup keeps reading the actions it asked for.
// Pointing the world at a *different* profile asset is a different matter --
// that is a new vocabulary, and a game that does it re-resolves its names.
//=============================================================================

struct InputProfileHandle
{
    DataAssetHandle Value;

    [[nodiscard]] bool IsValid() const { return Value.IsValid(); }
};

// Where a profile's compiled tables stand relative to the assets behind them.
enum class InputBindState : std::uint8_t
{
    Unbound,   // never attempted
    Current,   // the tables came from the newest revision of both assets
    Stale,     // the newest revision failed; the last good tables are still serving
    Failed,    // the newest revision failed and there was nothing to fall back on
};

// Why the controls are behaving the way they are.
//
// Diagnostics belong to the entry, not to whoever asked first: a startup call
// and a per-frame reporter both need to see the same failure. `Revision` moves
// only when the diagnostics change, which is what lets a reporter log once per
// change instead of once per frame.
struct InputBindStatus
{
    InputBindState State = InputBindState::Unbound;
    std::uint64_t Revision = 0;
    std::span<const std::string> Errors;
};

// The diagnostics as one line, for a log or a diagnostic surface. Empty when
// the profile bound cleanly.
[[nodiscard]] std::string DescribeBindErrors(const InputBindStatus& status);

class InputBindingCache
{
public:
    explicit InputBindingCache(DataAssetCache& dataAssets);

    // The compiled tables for a profile, or null when there are none to serve.
    // Bindings that fail individually are dropped and the rest still bind, so
    // one bad entry costs its own controls rather than every control; ask
    // Status() why.
    [[nodiscard]] const BoundInputProfile* Get(InputProfileHandle handle);

    // The action registry a profile's action set produced. Consumers resolve
    // their action names against this once and index by id afterwards.
    [[nodiscard]] const InputActionRegistry* GetActions(InputProfileHandle handle);

    // State and diagnostics as of the last Get()/GetActions() for this profile.
    [[nodiscard]] InputBindStatus Status(InputProfileHandle handle) const;

    void Clear();
    [[nodiscard]] std::size_t Size() const { return Entries.size(); }
    [[nodiscard]] std::uint64_t RebuildCount() const { return Rebuilds; }

private:
    struct Entry
    {
        DataAssetCacheHandle ProfileLease;
        DataAssetCacheHandle ActionSetLease;
        // The path the action-set lease was taken for. A profile edit can name
        // a different set, and the lease and its reload version follow it.
        std::string ActionSetPath;
        InputActionRegistry Actions;
        BoundInputProfile Profile;
        std::uint64_t ProfileVersion = 0;
        std::uint64_t ActionSetVersion = 0;
        InputBindState State = InputBindState::Unbound;
        // Every binding that did not resolve, not just the first: fixing one
        // should not be how the author discovers the next.
        std::vector<std::string> Errors;
        std::uint64_t ErrorRevision = 0;
    };

    Entry* Resolve(InputProfileHandle handle);

    DataAssetCache& DataAssets;
    std::unordered_map<std::uint64_t, Entry> Entries;
    std::uint64_t Rebuilds = 0;
};
