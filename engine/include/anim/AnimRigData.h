#pragma once

#include <anim/AnimTypes.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>

#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// Rig (`animation.rig`)
//
// What one kind of animated entity is: its skeleton, the fact schema its rules
// read, the request schema its requests are checked against, how much fact
// storage it carries, and its ordered layers. The one file a new entity type
// starts from.
//
// Tier is not declared here. It follows from which components an entity ends
// up carrying, and a rig without facts is a Prop rig: requests and content,
// nothing else. The compiled value names everything and binds nothing; the
// World-local view is AnimRigBinding.
//=============================================================================

inline constexpr std::string_view kAnimRigType = "animation.rig";

enum class AnimLayerMode : std::uint8_t
{
    Override,
    Additive,
};

struct AnimRigLayer
{
    // A gameplay tag naming the layer: Anim.Layer.Base, Anim.Layer.Upper.
    std::string Name;
    AnimLayerMode Mode = AnimLayerMode::Override;
    // Constant weight. A layer whose weight is a rule output takes it from its
    // selector instead.
    float Weight = 1.0f;
};

struct AnimRigData
{
    std::string SkeletonPath;
    std::string FactSchemaPath;
    std::string RequestSchemaPath;
    AnimFactCapacity FactCapacity = AnimFactCapacity::Small;
    std::vector<AnimRigLayer> Layers;

    [[nodiscard]] bool HasFacts() const { return !FactSchemaPath.empty(); }
};

void RegisterAnimRigData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
void UnregisterAnimRigData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
