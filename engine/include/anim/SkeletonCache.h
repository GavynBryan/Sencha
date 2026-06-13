#pragma once

#include <anim/Skeleton.h>
#include <anim/SkeletonHandle.h>
#include <core/assets/AssetCache.h>

#include <cstdint>
#include <string>
#include <string_view>

struct SkeletonEntry
{
    SkeletonData Value;
    uint32_t Generation = 0;
    uint32_t RefCount = 0;
    std::string PathKey;
    bool Alive = false;
};

//=============================================================================
// SkeletonCache (docs/assets/pipeline.md, Decision J)
//
// CPU-side skeleton cache with stable string keys — the MaterialCache
// pattern. Skinned mesh entries and animation clip entries hold RAII
// references into it; the skeleton outlives everything that poses against
// it. No file IO (Decision I): Acquire resolves registered entries only.
//=============================================================================
class SkeletonCache final : public AssetCache<SkeletonCache, SkeletonHandle, SkeletonEntry>
{
public:
    SkeletonCache();
    ~SkeletonCache() override;

    SkeletonCache(const SkeletonCache&) = delete;
    SkeletonCache& operator=(const SkeletonCache&) = delete;
    SkeletonCache(SkeletonCache&&) = delete;
    SkeletonCache& operator=(SkeletonCache&&) = delete;

    [[nodiscard]] SkeletonHandle Register(std::string_view name, SkeletonData skeleton);

    [[nodiscard]] SkeletonHandle Acquire(std::string_view name);
    [[nodiscard]] SkeletonCacheHandle AcquireOwned(std::string_view name);
    [[nodiscard]] SkeletonHandle Find(std::string_view name) const;

    [[nodiscard]] const SkeletonData* Get(SkeletonHandle handle) const;
    [[nodiscard]] std::string_view GetName(SkeletonHandle handle) const;

private:
    friend class AssetCache<SkeletonCache, SkeletonHandle, SkeletonEntry>;

    bool OnLoad(std::string_view path, SkeletonEntry& out);
    void OnFree(SkeletonEntry& entry);
    bool IsEntryLive(const SkeletonEntry& entry) const;
};
