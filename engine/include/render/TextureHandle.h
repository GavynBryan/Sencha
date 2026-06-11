#pragma once
#include <core/handle/LifetimeHandle.h>

#include <cstdint>

//=============================================================================
// TextureHandle
//
// Opaque handle type for referring to texture resources without exposing a
// graphics backend handle.
//==============================================================================
struct TextureHandle
{
    uint32_t Id = 0;

    [[nodiscard]] bool IsValid() const { return Id != 0; }
    [[nodiscard]] bool IsNull()  const { return Id == 0; }
    bool operator==(const TextureHandle&) const = default;
};

// RAII texture reference. The alias lives here rather than in TextureCache.h
// so backend-free code (MaterialCache entries, headless tests) can own
// texture lifetimes through the type-erased ILifetimeOwner without seeing
// Vulkan headers.
class TextureCache;
using TextureCacheHandle = LifetimeHandle<TextureCache, TextureHandle>;
