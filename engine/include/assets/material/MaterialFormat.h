#pragma once

#include <core/assets/AssetRef.h>
#include <render/Material.h>

//=============================================================================
// MaterialDescription
//
// The authored form of a material: the .smat JSON schema as plain data
// (docs/assets/pipeline.md, Decision L — the glTF metallic-roughness model).
// Texture slots are stable AssetRefs; converting them into runtime
// descriptor indices is AssetSystem's job, not the parser's.
//
// Every texture slot is optional with a defined neutral default (white base
// color, flat +Z normal, occlusion 1 / roughness 1 / metallic 0 ORM, black
// emissive): a description with no textures is still a complete material.
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

    Vec4 EmissiveFactor = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    AssetRef EmissiveTexture;

    MaterialAlphaMode AlphaMode = MaterialAlphaMode::Opaque;
    float AlphaCutoff = 0.5f;
};

inline constexpr uint32_t kSmatVersion = 1;
