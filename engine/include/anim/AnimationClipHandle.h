#pragma once
#include <core/handle/Handle.h>
#include <core/handle/Owned.h>

//=============================================================================
// AnimationClipHandle
//
// Opaque generational handle into AnimationClipCache (docs/assets/
// pipeline.md, Decision J). Distinct from AudioClipHandle — "clip" is
// overloaded in this engine; the types are not. One of the engine's unified
// Handle<Tag> types.
//=============================================================================
using AnimationClipHandle = Handle<struct AnimationClipHandleTag>;

class AnimationClipCache;
using AnimationClipCacheHandle = Owned<AnimationClipHandle>;
