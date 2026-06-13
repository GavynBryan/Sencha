#pragma once
#include <core/handle/LifetimeHandle.h>

#include <cstdint>

//=============================================================================
// SkeletonHandle
//
// Opaque generational handle into SkeletonCache (docs/assets/pipeline.md,
// Decision J).
//=============================================================================
struct SkeletonHandle
{
    uint32_t Id = 0;

    [[nodiscard]] bool IsValid() const { return Id != 0; }
    [[nodiscard]] bool IsNull()  const { return Id == 0; }
    bool operator==(const SkeletonHandle&) const = default;
};

// RAII skeleton reference. The alias lives here (the TextureHandle.h
// precedent) so cache entries that *hold* skeletons — animation clips,
// skinned mesh entries — can own the lifetime through the type-erased
// ILifetimeOwner without including the cache header.
class SkeletonCache;
using SkeletonCacheHandle = LifetimeHandle<SkeletonCache, SkeletonHandle>;
