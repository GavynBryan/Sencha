#pragma once

#include <math/Vec.h>

#include <cstdint>

// Packed versioned handle to a material owned by MaterialCache. Id 0 is null.
struct MaterialHandle
{
    uint32_t Id = 0;

    [[nodiscard]] bool IsValid() const { return Id != 0; }
    [[nodiscard]] bool IsNull() const { return Id == 0; }
    [[nodiscard]] uint32_t SlotIndex() const { return Id & ((1u << 20u) - 1u); }
    [[nodiscard]] uint32_t Generation() const { return Id >> 20u; }
    bool operator==(const MaterialHandle&) const = default;
};

// Identifies the render pass a material belongs to. Used as the high bits of the sort key.
enum class ShaderPassId : uint16_t
{
    ForwardOpaque = 0
};

//=============================================================================
// Material
//
// CPU-side material descriptor. Specifies which shader pass to use and
// surface parameters. Owned and versioned by MaterialCache; accessed via
// MaterialHandle.
//
// BaseColorTextureIndex == UINT32_MAX means no texture; shaders treat it as
// white and multiply by BaseColor.
//=============================================================================
struct Material
{
    ShaderPassId Pass = ShaderPassId::ForwardOpaque;
    Vec4 BaseColor = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    uint32_t BaseColorTextureIndex = UINT32_MAX;
};
