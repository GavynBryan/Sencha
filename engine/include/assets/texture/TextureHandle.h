// render/handles/TextureHandle.h
#pragma once
#include <cstdint>

//=============================================================================
// TextureHandle
//
// Opaque handle type for referring to CPU-side texture resources.
//==============================================================================
struct TextureHandle
{
    uint32_t Id = 0;

    [[nodiscard]] bool IsValid() const { return Id != 0; }
    [[nodiscard]] bool IsNull()  const { return Id == 0; }
    bool operator==(const TextureHandle&) const = default;
};