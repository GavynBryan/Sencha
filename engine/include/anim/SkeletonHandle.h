#pragma once
#include <core/handle/Handle.h>
#include <core/handle/Owned.h>

//=============================================================================
// SkeletonHandle
//
// Opaque generational handle into SkeletonCache (docs/assets/pipeline.md,
// Decision J). One of the engine's unified Handle<Tag> types.
//=============================================================================
using SkeletonHandle = Handle<struct SkeletonHandleTag>;

// RAII skeleton reference. The alias lives here (the TextureHandle.h
// precedent) so cache entries that *hold* skeletons — animation clips,
// skinned mesh entries — can own the lifetime through the type-erased
// ILifetimeOwner without including the cache header.
class SkeletonCache;
using SkeletonCacheHandle = Owned<SkeletonHandle>;
