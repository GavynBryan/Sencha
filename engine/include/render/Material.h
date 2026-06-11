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

// Authored alpha behavior (docs/assets/pipeline.md, Decision L). Blend maps
// to a transparent phase that has no pipeline yet: loaders accept it, warn,
// and the material renders opaque until that phase exists.
enum class MaterialAlphaMode : uint8_t
{
    Opaque = 0,
    Mask = 1,
    Blend = 2,
};

//=============================================================================
// Material
//
// CPU-side material descriptor: the runtime form of the .smat PBR schema
// (glTF metallic-roughness model; docs/assets/pipeline.md, Decision L).
// Owned and versioned by MaterialCache; accessed via MaterialHandle.
//
// Texture slots hold bindless descriptor indices. UINT32_MAX means "no
// texture"; shaders substitute the slot's neutral default (white base color,
// flat +Z normal, occlusion 1 / roughness 1 / metallic 0 ORM, black
// emissive) and apply the factors, so a material with no textures is still
// a complete PBR material. The current forward shader consumes BaseColor
// only; the remaining slots ride the data until the PBR pass lands.
//=============================================================================
struct Material
{
    ShaderPassId Pass = ShaderPassId::ForwardOpaque;

    Vec4 BaseColor = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    Vec4 EmissiveFactor = Vec4(0.0f, 0.0f, 0.0f, 0.0f);

    uint32_t BaseColorTextureIndex = UINT32_MAX;
    uint32_t NormalTextureIndex = UINT32_MAX;
    uint32_t OrmTextureIndex = UINT32_MAX;
    uint32_t EmissiveTextureIndex = UINT32_MAX;

    float NormalScale = 1.0f;
    float RoughnessFactor = 1.0f;
    float MetallicFactor = 0.0f;

    MaterialAlphaMode AlphaMode = MaterialAlphaMode::Opaque;
    float AlphaCutoff = 0.5f;
};
