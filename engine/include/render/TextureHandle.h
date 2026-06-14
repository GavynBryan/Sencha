#pragma once
#include <core/handle/Handle.h>
#include <core/handle/Owned.h>

//=============================================================================
// TextureHandle
//
// Opaque handle to a texture in TextureCache, without exposing a graphics
// backend handle. One of the engine's unified Handle<Tag> types (handle
// convergence).
//==============================================================================
using TextureHandle = Handle<struct TextureHandleTag>;

// RAII texture reference. The alias lives here rather than in TextureCache.h
// so backend-free code (MaterialCache entries, headless tests) can own
// texture lifetimes through the type-erased ILifetimeOwner without seeing
// Vulkan headers.
class TextureCache;
using TextureCacheHandle = Owned<TextureHandle>;
