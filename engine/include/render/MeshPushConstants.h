#pragma once

#include <render/MaterialShaderCatalog.h>
#include <math/Vec.h>

#include <array>
#include <cstdint>

// The GLSL twin is MeshPush in engine/shaders/mesh_material.glsli.
struct MeshPushConstants
{
    Vec4 BaseColor;
    Vec4 EmissiveFactor;
    float NormalScale = 1.0f;
    float RoughnessFactor = 1.0f;
    float MetallicFactor = 0.0f;
    float SpecularIntensity = 0.5f;
    std::uint32_t BaseColorTextureIndex = UINT32_MAX;
    std::uint32_t NormalTextureIndex = UINT32_MAX;
    std::uint32_t OrmTextureIndex = UINT32_MAX;
    std::uint32_t EmissiveTextureIndex = UINT32_MAX;
    std::uint32_t ReceiveShadows = 1;
    // Bindless slot of the zone's baked-lighting atlas; UINT32_MAX skips the
    // baked term. Uniform per run (part of the run-merge identity).
    std::uint32_t LightmapTextureIndex = UINT32_MAX;
    // Bindless slot of the zone's baked-AO plane (lightmap UVs); UINT32_MAX
    // leaves ambient unmodulated. Uniform per run, merge identity too.
    std::uint32_t AoTextureIndex = UINT32_MAX;
    // Fragments whose base-colour alpha falls below this are discarded, in the
    // masked pipeline variants only. Ignored by the others, so an opaque
    // material keeps its early-depth path.
    float AlphaCutoff = 0.0f;
    std::array<Vec4, kMaterialShaderParamCount> Params{};
};
