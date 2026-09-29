#pragma once

#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

inline constexpr std::string_view kAnimBlendspaceType = "animation.blendspace";
inline constexpr std::size_t kAnimBlendspaceMaxAxes = 2;
inline constexpr std::size_t kAnimBlendspaceMaxSamples = 16;

struct AnimBlendspaceAxisDecl
{
    // A float or int fact of the rig playing the blendspace.
    std::string Fact;
    // The fact is clamped to this range before the samples are weighted.
    float Min = 0.0f;
    float Max = 1.0f;
};

struct AnimBlendspaceSampleDecl
{
    std::string Clip;
    // One coordinate per axis.
    float At[kAnimBlendspaceMaxAxes] = {};
};

struct AnimBlendspaceData
{
    std::vector<AnimBlendspaceAxisDecl> Axes;
    std::vector<AnimBlendspaceSampleDecl> Samples;
};

void RegisterAnimBlendspaceData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
