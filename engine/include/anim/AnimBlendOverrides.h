#pragma once

#include <anim/AnimBehaviorSet.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>

#include <string>
#include <string_view>
#include <vector>

inline constexpr std::string_view kAnimBlendOverridesType = "animation.blend_overrides";

struct AnimBlendOverrideDecl
{
    std::string From;
    std::string To;
    AnimBlendPolicy Blend;
};

struct AnimBlendOverrides
{
    std::vector<AnimBlendOverrideDecl> Overrides;
};

void RegisterAnimBlendOverrides(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
