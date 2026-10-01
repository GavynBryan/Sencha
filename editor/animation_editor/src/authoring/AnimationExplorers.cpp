#include "authoring/AnimationExplorers.h"

#include "authoring/AnimationPreviewWorkspace.h"

#include <anim/AnimBehaviorSet.h>
#include <anim/AnimBlendOverrides.h>
#include <anim/AnimBlendspaceData.h>
#include <anim/AnimFactSchema.h>
#include <anim/AnimFlowData.h>
#include <anim/AnimRequestSchema.h>
#include <anim/AnimRigData.h>
#include <anim/AnimSelectorData.h>
#include <anim/AnimSlotMapData.h>
#include <anim/Skeleton.h>

#include <array>
#include <charconv>
#include <format>
#include <functional>
#include <span>

namespace
{
using Kind = AnimationExplorerItem::Kind;

struct KindName
{
    Kind What;
    std::string_view Prefix;
};
constexpr std::array<KindName, 8> kPrefixes{ {
    { Kind::Group, "group:" },
    { Kind::Mesh, "mesh:" },
    { Kind::Skeleton, "skeleton:" },
    { Kind::Clip, "clip:" },
    { Kind::Material, "material:" },
    { Kind::Rig, "rig:" },
    { Kind::Document, "document:" },
    { Kind::Joint, "joint:" },
} };

std::string KeyOf(Kind what, std::string_view path)
{
    for (const KindName& name : kPrefixes)
        if (name.What == what)
            return std::string(name.Prefix) + std::string(path);
    return std::string(path);
}

struct DocumentGroup
{
    std::string_view Title;
    std::string_view Subtype;
};
constexpr std::array<DocumentGroup, 8> kDocumentGroups{ {
    { "Selectors", kAnimSelectorType },
    { "Behavior sets", kAnimBehaviorSetType },
    { "Slot maps", kAnimSlotMapType },
    { "Flows", kAnimFlowType },
    { "Blendspaces", kAnimBlendspaceType },
    { "Blend overrides", kAnimBlendOverridesType },
    { "Fact schemas", kAnimFactSchemaType },
    { "Request schemas", kAnimRequestSchemaType },
} };

void Group(std::vector<OutlineNode>& nodes, std::string_view title, Kind what, std::span<const std::string> paths,
           std::string_view current)
{
    nodes.push_back({ .Key = KeyOf(Kind::Group, title), .Label = std::string(title) });
    for (const std::string& path : paths)
        nodes.push_back({ .Key = KeyOf(what, path), .Label = path, .Depth = 1, .Current = path == current });
}
} // namespace

std::optional<AnimationExplorerItem> ParseAnimationExplorerKey(std::string_view key)
{
    for (const KindName& name : kPrefixes)
    {
        if (!key.starts_with(name.Prefix))
            continue;
        const std::string_view rest = key.substr(name.Prefix.size());
        if (name.What != Kind::Joint)
            return AnimationExplorerItem{ .What = name.What, .Path = std::string(rest) };
        int joint = -1;
        if (std::from_chars(rest.data(), rest.data() + rest.size(), joint).ec != std::errc{})
            return std::nullopt;
        return AnimationExplorerItem{ .What = Kind::Joint, .Joint = joint };
    }
    return std::nullopt;
}

std::vector<OutlineNode> AnimationContentNodes(const AnimationPreviewWorkspace& workspace)
{
    const AnimationAuditionSelection& audition = workspace.Audition;
    std::vector<OutlineNode> nodes;
    Group(nodes, "Skinned meshes", Kind::Mesh, workspace.Content.Of(AssetType::SkinnedMesh), audition.MeshPath);
    Group(nodes, "Skeletons", Kind::Skeleton, workspace.Content.Of(AssetType::Skeleton),
          audition.HasMesh() ? std::string_view{} : std::string_view(audition.Session.SkeletonPath()));
    Group(nodes, "Clips", Kind::Clip, workspace.Content.Of(AssetType::AnimationClip), audition.ClipPath);
    Group(nodes, "Materials", Kind::Material, workspace.Content.Of(AssetType::Material), audition.MaterialPath);
    return nodes;
}

std::vector<OutlineNode> AnimationRigNodes(const AnimationPreviewWorkspace& workspace)
{
    std::vector<OutlineNode> nodes;
    Group(nodes, "Rigs", Kind::Rig, workspace.Content.OfSubtype(kAnimRigType), workspace.Rig.Path);
    const DataDocument* active = workspace.Documents.Active();
    const std::string_view current = active != nullptr ? std::string_view(active->VirtualPath()) : std::string_view{};
    for (const DocumentGroup& group : kDocumentGroups)
        if (const std::span<const std::string> paths = workspace.Content.OfSubtype(group.Subtype); !paths.empty())
            Group(nodes, group.Title, Kind::Document, paths, current);
    return nodes;
}

std::vector<OutlineNode> AnimationSkeletonNodes(const SkeletonData& skeleton, int picked)
{
    // Parents come before their children, but a parent's subtrees need not be
    // contiguous: walk the children to put each under its parent.
    const std::size_t count = skeleton.Joints.size();
    std::vector<std::vector<std::size_t>> children(count);
    std::vector<std::size_t> roots;
    for (std::size_t joint = 0; joint < count; ++joint)
    {
        const std::int32_t parent = skeleton.Joints[joint].ParentIndex;
        if (parent >= 0 && static_cast<std::size_t>(parent) < count)
            children[static_cast<std::size_t>(parent)].push_back(joint);
        else
            roots.push_back(joint);
    }
    std::vector<OutlineNode> nodes;
    nodes.reserve(count);
    const std::function<void(std::size_t, int)> visit = [&](std::size_t joint, int depth) {
        const std::string& name = skeleton.Joints[joint].Name;
        nodes.push_back({ .Key = KeyOf(Kind::Joint, std::to_string(joint)),
                          .Label = name.empty() ? std::format("Joint {}", joint) : name,
                          .Depth = depth,
                          .Current = static_cast<int>(joint) == picked });
        for (const std::size_t child : children[joint])
            visit(child, depth + 1);
    };
    for (const std::size_t root : roots)
        visit(root, 0);
    return nodes;
}
