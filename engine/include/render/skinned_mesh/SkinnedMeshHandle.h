#pragma once
#include <core/handle/Handle.h>
#include <core/handle/Owned.h>

//=============================================================================
// SkinnedMeshHandle
//
// Opaque generational handle into SkinnedMeshCache. Distinct from
// StaticMeshHandle: a skinned mesh is a different asset type with a different
// runtime (pose evaluation, a skinning pass, per-instance posed buffers —
// Decision N), so the handle types do not interchange. One of the engine's
// unified Handle<Tag> types (handle convergence).
//=============================================================================
using SkinnedMeshHandle = Handle<struct SkinnedMeshHandleTag>;

class SkinnedMeshCache;
using SkinnedMeshCacheHandle = Owned<SkinnedMeshHandle>;
