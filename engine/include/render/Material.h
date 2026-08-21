#pragma once

#include <core/handle/Handle.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <math/Vec.h>

#include <cstdint>
#include <string_view>
#include <tuple>

// Versioned handle to a material owned by MaterialCache. Slot 0 is null. One
// of the engine's unified Handle<Tag> types (handle convergence); transient,
// so it carries no reflection. Scene data references materials by asset path.
using MaterialHandle = Handle<struct MaterialHandleTag>;

// Identifies the render pass a material belongs to. Used as the high bits of the sort key.
enum class ShaderPassId : uint16_t
{
    ForwardOpaque = 0,
    // Blended geometry, drawn after every opaque item, back-to-front per view.
    ForwardTransparent = 1,
};

enum class MaterialShading : uint8_t
{
    StandardLit = 0,
    Unlit = 1,
};

// Authored alpha behavior. Blend maps to a transparent phase that has no
// pipeline yet: loaders accept it, warn, and the material renders opaque until
// that phase exists.
enum class MaterialAlphaMode : uint8_t
{
    Opaque = 0,
    Mask = 1,
    Blend = 2,
};

//=============================================================================
// Material
//
// CPU-side material descriptor: the runtime form of the .smat material data.
// Owned and versioned by MaterialCache; accessed through MaterialHandle.
//
// Texture slots hold bindless descriptor indices. UINT32_MAX means no texture.
// Shaders substitute the slot's neutral default and apply the factors, so a
// material with no textures remains complete.
//=============================================================================
struct Material
{
    ShaderPassId Pass = ShaderPassId::ForwardOpaque;
    MaterialShading Shading = MaterialShading::StandardLit;

    Vec4 BaseColor = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    Vec4 EmissiveFactor = Vec4(0.0f, 0.0f, 0.0f, 0.0f);

    uint32_t BaseColorTextureIndex = UINT32_MAX;
    uint32_t NormalTextureIndex = UINT32_MAX;
    uint32_t OrmTextureIndex = UINT32_MAX;
    uint32_t EmissiveTextureIndex = UINT32_MAX;

    float NormalScale = 1.0f;
    float RoughnessFactor = 1.0f;
    float MetallicFactor = 0.0f;
    float SpecularIntensity = 0.5f;
    float EmissiveStrength = 1.0f;

    MaterialAlphaMode AlphaMode = MaterialAlphaMode::Opaque;
    float AlphaCutoff = 0.5f;
    bool DoubleSided = false;
    bool ReceiveShadows = true;
    bool CastShadows = true;
};
