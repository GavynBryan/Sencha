#pragma once

#include "workspaces/TreeOutline.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class AnimationPreviewWorkspace;
struct SkeletonData;

// What an animation explorer's item names: a group to open or close, content
// to audition, a rig to open, a document to edit, a joint to pick.
struct AnimationExplorerItem
{
    enum class Kind : std::uint8_t
    {
        Group,
        Mesh,
        Skeleton,
        Clip,
        Material,
        Rig,
        Document,
        Joint,
    };
    Kind What = Kind::Group;
    std::string Path = {};
    int Joint = -1;
};

[[nodiscard]] std::optional<AnimationExplorerItem> ParseAnimationExplorerKey(std::string_view key);

// What can be auditioned: skinned meshes, skeletons, clips and material
// overrides, each a group, the one in use current.
[[nodiscard]] std::vector<OutlineNode> AnimationContentNodes(const AnimationPreviewWorkspace& workspace);
// The project's rigs, then its animation documents by subtype.
[[nodiscard]] std::vector<OutlineNode> AnimationRigNodes(const AnimationPreviewWorkspace& workspace);
// The skeleton's joints, each under its parent; `picked` is current.
[[nodiscard]] std::vector<OutlineNode> AnimationSkeletonNodes(const SkeletonData& skeleton, int picked);
