#pragma once

#include <core/handle/Handle.h>

//=============================================================================
// MaterialSetHandle
//
// Handle to an ordered, immutable set of materials owned by MaterialSetCache.
// One of the engine's unified Handle<Tag> types. Transient: scene data persists
// the material paths, never this handle (the StaticMeshHandle / MaterialHandle
// rule).
//
// The alias lives here rather than in MaterialSetCache.h so a component can
// name what it holds without pulling in the cache that owns it -- the same
// split TextureHandle already has.
//=============================================================================
using MaterialSetHandle = Handle<struct MaterialSetHandleTag>;
