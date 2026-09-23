#pragma once

#include <anim/AnimPredicate.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// Slot map (`animation.slot_map`)
//
// One rig's answer to "what plays for this behavior": ordered rows of a
// behavior tag, a predicate over facts, and content. Resolution is first match
// and memoryless, and runs every tick, so content can change under a stable
// behavior -- the reload that becomes reload_shotgun when WeaponType says so.
//
// A rig stacks slot maps: a base map, then overlays. Rows merge by priority,
// higher first, and within a priority by stack order, so an overlay adds or
// shadows content without editing the base map's rows.
//=============================================================================

inline constexpr std::string_view kAnimSlotMapType = "animation.slot_map";

struct AnimSlotRowDecl
{
    std::string Behavior;
    std::int32_t Priority = 0;
    AnimPredicateDecl When;
    // Exactly one: an animation clip ("asset://...sanim"), or a flow
    // ("asset://...sdata") for a behavior whose content is a sequence.
    std::string Clip;
    std::string Flow;
};

struct AnimSlotMapData
{
    std::vector<AnimSlotRowDecl> Rows;
};

void RegisterAnimSlotMapData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
