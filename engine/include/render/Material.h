#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <math/Vec.h>

#include <cstdint>
#include <string_view>
#include <tuple>

// Versioned handle to a material owned by MaterialCache. Slot 0 is null.
struct MaterialHandle
{
    uint32_t Index = 0;
    uint32_t Generation = 0;

    [[nodiscard]] bool IsValid() const { return Index != 0 && Generation != 0; }
    [[nodiscard]] bool IsNull() const { return !IsValid(); }
    [[nodiscard]] uint32_t SlotIndex() const { return Index; }
    bool operator==(const MaterialHandle&) const = default;
};

template <>
struct TypeSchema<MaterialHandle>
{
    static constexpr std::string_view Name = "MaterialHandle";

    static auto Fields()
    {
        return std::tuple{
            MakeField("index", &MaterialHandle::Index),
            MakeField("generation", &MaterialHandle::Generation),
        };
    }
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
