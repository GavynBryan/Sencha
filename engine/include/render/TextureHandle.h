#pragma once
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
