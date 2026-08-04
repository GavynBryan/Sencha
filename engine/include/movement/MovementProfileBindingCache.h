#pragma once

#include <assets/data/DataAssetCache.h>
#include <movement/MovementProfileData.h>
#include <movement/MovementProfileRuntime.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

class GameplayTagRegistry;
class LocomotionModeRegistry;

class MovementProfileBindingCache
{
public:
    MovementProfileBindingCache(DataAssetCache& dataAssets,
                                GameplayTagRegistry& tags,
                                LocomotionModeRegistry& modes);

    [[nodiscard]] const BoundMovementProfile* Get(MovementProfileHandle handle,
                                                  std::string* newError = nullptr);
    void Clear();
    [[nodiscard]] std::size_t Size() const { return Entries.size(); }
    [[nodiscard]] uint64_t RebuildCount() const { return Rebuilds; }

private:
    struct Entry
    {
        DataAssetCacheHandle Lease;
        BoundMovementProfile Profile;
        uint64_t ReloadVersion = 0;
        uint64_t ModeGeneration = 0;
        std::size_t TagCount = 0;
        std::string Error;
        bool ErrorDelivered = false;
    };

    DataAssetCache& DataAssets;
    GameplayTagRegistry& Tags;
    LocomotionModeRegistry& Modes;
    std::unordered_map<uint64_t, Entry> Entries;
    uint64_t Rebuilds = 0;
};
