#pragma once

#include <anim/AnimPredicate.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

inline constexpr std::string_view kAnimSlotMapType = "animation.slot_map";

struct AnimSlotRowDecl
{
    std::string Behavior;
    std::int32_t Priority = 0;
    AnimPredicateDecl When;
    // Exactly one: a clip, a flow for sequenced content, or a blendspace for content
    // placed by facts.
    std::string Clip;
    std::string Flow;
    std::string Blendspace;
};

struct AnimSlotMapData
{
    std::vector<AnimSlotRowDecl> Rows;
};

void RegisterAnimSlotMapData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
