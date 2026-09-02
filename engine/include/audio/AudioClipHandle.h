#pragma once

#include <core/handle/Handle.h>

//=============================================================================
// AudioClipHandle
//
// Opaque handle to a decoded clip in AudioClipCache. One of the engine's
// unified Handle<Tag> types.
//
// The alias lives here rather than in AudioClipCache.h so a component can name
// what it holds without pulling in the cache that owns it.
//=============================================================================
using AudioClipHandle = Handle<struct AudioClipHandleTag>;
