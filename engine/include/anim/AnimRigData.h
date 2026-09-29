#pragma once

#include <anim/AnimTypes.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>

#include <string>
#include <string_view>
#include <vector>

inline constexpr std::string_view kAnimRigType = "animation.rig";

enum class AnimLayerMode : std::uint8_t
{
    Override,
    Additive,
};

// Applied in order to a mask that starts empty: a joint by skeleton name, with or
// without its subtree, added or excluded.
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
    // Constant weight; a weight rule in the selector replaces it.
    float Weight = 1.0f;
    // Empty makes the layer request-keyed: the newest request claiming it names the behavior.
    std::string SelectorPath;
    // The behavior played when nothing is selected or requested.
    std::string Idle;
    // Empty leaves the layer unmasked. The first layer may not be masked, since the
    // others compose onto it.
    std::vector<AnimMaskOp> Mask;
};

// A selector bound to an extension point selectors expose, so a game or mod adds
// rules without editing the selector that declares the point.
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
    // A later set adds behaviors or overrides an earlier set's policy by tag.
    std::vector<std::string> BehaviorSetPaths;
    // The base map first, then overlays.
    std::vector<std::string> SlotMapPaths;
    // Bindings the rig's events name. Two declaring one key conflict; neither overrides.
    std::vector<std::string> BindingSetPaths;
    // A later asset replaces an earlier one's policy for the same pair.
    std::vector<std::string> BlendOverridePaths;
    std::vector<AnimRigExtension> Extensions;

    [[nodiscard]] bool HasFacts() const { return !FactSchemaPath.empty(); }
};

void RegisterAnimRigData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
void UnregisterAnimRigData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
