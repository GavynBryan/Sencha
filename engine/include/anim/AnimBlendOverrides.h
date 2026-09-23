#pragma once

#include <anim/AnimBehaviorSet.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>

#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// Blend overrides (`animation.blend_overrides`)
//
// Pairwise exceptions to the rule that a change blends by the policy of the
// behavior it goes to: sprint to slide, fall to land. An override names the
// behavior a layer leaves and the one it enters and replaces only the blend
// policy for that change -- never what is selected or what plays. A rig lists
// override assets in order, a later one replacing an earlier one's entry for
// the same pair, and binds at most a capped number of pairs: a rig that needs
// more wants another behavior or a fact, not another override.
//=============================================================================

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
