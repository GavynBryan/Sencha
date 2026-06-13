#pragma once
#include <core/handle/LifetimeHandle.h>

#include <cstdint>

//=============================================================================
// SkinnedMeshHandle
//
// Opaque generational handle into SkinnedMeshCache. Distinct from
// StaticMeshHandle: a skinned mesh is a different asset type with a different
// runtime (pose evaluation, a skinning pass, per-instance posed buffers —
// Decision N), so the handle types do not interchange.
//=============================================================================
struct SkinnedMeshHandle
{
    uint32_t Index = 0;
    uint32_t Generation = 0;

    [[nodiscard]] bool IsValid() const { return Index != 0 && Generation != 0; }
    [[nodiscard]] bool IsNull() const { return !IsValid(); }
    bool operator==(const SkinnedMeshHandle&) const = default;
};

class SkinnedMeshCache;
using SkinnedMeshCacheHandle = LifetimeHandle<SkinnedMeshCache, SkinnedMeshHandle>;
