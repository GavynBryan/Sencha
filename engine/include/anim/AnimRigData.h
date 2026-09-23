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
// storage it carries, its ordered layers and the selector on each, the
// behaviors it plays, the slot maps that give those behaviors content, and the
// authored bindings its events invoke through. The one file a new entity type
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

// One step of a layer's bone mask, applied in order to a set that starts
// empty: a joint, named as the skeleton names it, with or without everything
// below it, taken into the mask or out of it.
struct AnimMaskOp
{
    std::string Joint;
    bool Exclude = false;
    bool Subtree = true;
};

struct AnimRigLayer
{
    // A gameplay tag naming the layer: Anim.Layer.Base, Anim.Layer.Upper.
    std::string Name;
    AnimLayerMode Mode = AnimLayerMode::Override;
    // Constant weight. A layer whose weight is a rule output takes it from its
    // selector instead.
    float Weight = 1.0f;
    // The selector choosing this layer's behavior ("asset://..."). Empty makes
    // the layer request-keyed: the newest request claiming it names the
    // behavior, which is the whole of the Prop tier.
    std::string SelectorPath;
    // The behavior played when nothing is selected or requested.
    std::string Idle;
    // Empty leaves the layer unmasked. Only a layer above the first may be
    // masked: the first is the pose the others compose onto.
    std::vector<AnimMaskOp> Mask;
};

// A selector bound to a name selectors expose, so a game or mod adds rules
// (a new weapon's) without editing the selector that declares the point.
struct AnimRigExtension
{
    std::string Name;
    std::string SelectorPath;
};

struct AnimRigData
{
    std::string SkeletonPath;
    std::string FactSchemaPath;
    std::string RequestSchemaPath;
    AnimFactCapacity FactCapacity = AnimFactCapacity::Small;
    std::vector<AnimRigLayer> Layers;
    // In order: a later set adds behaviors or overrides an earlier set's
    // policy by tag.
    std::vector<std::string> BehaviorSetPaths;
    // The base map first, then overlays.
    std::vector<std::string> SlotMapPaths;
    // `authored.bindings` assets, in order, that the rig's clip events and
    // lifecycle events name their bindings from. Two that declare one key are
    // an authoring conflict, not an override.
    std::vector<std::string> BindingSetPaths;
    std::vector<AnimRigExtension> Extensions;

    [[nodiscard]] bool HasFacts() const { return !FactSchemaPath.empty(); }
};

void RegisterAnimRigData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
void UnregisterAnimRigData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
