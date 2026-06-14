#pragma once

#include <anim/AnimationClip.h>
#include <anim/AnimationClipHandle.h>
#include <anim/SkeletonHandle.h>
#include <core/assets/AssetCache.h>

#include <cstdint>
#include <string>
#include <string_view>

struct AnimationClipEntry
{
    AnimationClipData Value;
    // RAII reference to the skeleton this clip poses (the MaterialEntry→
    // OwnedTextures pattern): releasing the clip releases the skeleton.
    SkeletonCacheHandle OwnedSkeleton;
    uint32_t Generation = 0;
    uint32_t RefCount = 0;
    std::string PathKey;
    bool Alive = false;
};

//=============================================================================
// AnimationClipCache (docs/assets/pipeline.md, Decision J)
//
// CPU-side animation clip cache with stable string keys. No file IO
// (Decision I): Acquire resolves registered entries only.
//=============================================================================
class AnimationClipCache final
    : public AssetCache<AnimationClipCache, AnimationClipHandle, AnimationClipEntry>
{
public:
    AnimationClipCache();
    ~AnimationClipCache() override;

    AnimationClipCache(const AnimationClipCache&) = delete;
    AnimationClipCache& operator=(const AnimationClipCache&) = delete;
    AnimationClipCache(AnimationClipCache&&) = delete;
    AnimationClipCache& operator=(AnimationClipCache&&) = delete;

    // Registers `clip`, taking ownership of the skeleton reference its
    // tracks pose. If `name` already exists, the existing handle is
    // returned and `ownedSkeleton` releases immediately (the existing
    // entry already owns its own reference).
    [[nodiscard]] AnimationClipHandle Register(std::string_view name,
                                               AnimationClipData clip,
                                               SkeletonCacheHandle ownedSkeleton);

    [[nodiscard]] AnimationClipHandle Acquire(std::string_view name);
    [[nodiscard]] AnimationClipCacheHandle AcquireOwned(std::string_view name);
    [[nodiscard]] AnimationClipHandle Find(std::string_view name) const;

    [[nodiscard]] const AnimationClipData* Get(AnimationClipHandle handle) const;
    [[nodiscard]] std::string_view GetName(AnimationClipHandle handle) const;

private:
    friend class AssetCache<AnimationClipCache, AnimationClipHandle, AnimationClipEntry>;

    bool OnLoad(std::string_view path, AnimationClipEntry& out);
    void OnFree(AnimationClipEntry& entry);
    bool IsEntryLive(const AnimationClipEntry& entry) const;
};
