#pragma once
#include <core/handle/LifetimeHandle.h>

#include <cstdint>

//=============================================================================
// AnimationClipHandle
//
// Opaque generational handle into AnimationClipCache (docs/assets/
// pipeline.md, Decision J). Distinct from AudioClipHandle — "clip" is
// overloaded in this engine; the types are not.
//=============================================================================
struct AnimationClipHandle
{
    uint32_t Id = 0;

    [[nodiscard]] bool IsValid() const { return Id != 0; }
    [[nodiscard]] bool IsNull()  const { return Id == 0; }
    bool operator==(const AnimationClipHandle&) const = default;
};

class AnimationClipCache;
using AnimationClipCacheHandle = LifetimeHandle<AnimationClipCache, AnimationClipHandle>;
