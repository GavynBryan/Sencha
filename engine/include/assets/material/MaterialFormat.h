#pragma once

#include <core/assets/AssetRef.h>
#include <render/Material.h>

//=============================================================================
// MaterialDescription
//
// The authored form of a material: the .smat JSON schema as plain data.
// Texture slots are stable AssetRefs; converting them into runtime descriptor
// indices is AssetSystem's job, not the parser's.
//
// Every texture slot is optional with a defined neutral default. A description
// with no textures remains a complete material.
//=============================================================================
struct MaterialDescription
{
    Vec4 BaseColorFactor = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    AssetRef BaseColorTexture;

    AssetRef NormalTexture;
    float NormalScale = 1.0f;

    AssetRef OrmTexture;
    float RoughnessFactor = 1.0f;
    float MetallicFactor = 0.0f;
    float SpecularIntensity = 0.5f;

    Vec4 EmissiveFactor = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    AssetRef EmissiveTexture;
    float EmissiveStrength = 1.0f;

    MaterialAlphaMode AlphaMode = MaterialAlphaMode::Opaque;
    float AlphaCutoff = 0.5f;
};

inline constexpr uint32_t kSmatVersion = 1;
