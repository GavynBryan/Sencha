#pragma once

#include <assets/data/DataAssetHandle.h>
#include <navigation/NavigationTypes.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class DataAssetCache;
class GameplayTagRegistry;

// Agents' navigation.policy assets bound to the running game's tags, rebound
// when an asset reloads or the tag registry grows. Each binding lives at a
// stable address, so a planning batch may point at it across the join.
class NavPolicyBindings
{
public:
    explicit NavPolicyBindings(const DataAssetCache* dataAssets) : DataAssets(dataAssets) {}

    // Null for no policy, or one that is not a resident navigation.policy.
    [[nodiscard]] const NavQueryPolicy* Bind(DataAssetHandle handle, const GameplayTagRegistry& tags);

    // Names a binding could not resolve, cleared by the next rebind of the same asset.
    [[nodiscard]] std::size_t UnresolvedNames() const;

private:
    struct Entry
    {
        std::uint64_t ReloadVersion = 0;
        std::size_t TagCount = 0;
        bool Resident = false;
        std::unique_ptr<NavQueryPolicy> Policy;
        std::vector<std::string> Unresolved;
    };

    const DataAssetCache* DataAssets = nullptr;
    std::unordered_map<std::uint64_t, Entry> Entries;
};
