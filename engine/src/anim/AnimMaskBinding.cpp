#include "AnimRigBinder.h"

#include <anim/AnimationClipCache.h>
#include <anim/SkeletonCache.h>

#include <format>
#include <unordered_map>

void AnimRigBinder::BindMasks(const AnimRigData& rig)
{
    bool masked = false;
    for (const AnimRigLayer& layer : rig.Layers)
        masked = masked || !layer.Mask.empty();

    const SkeletonData* skeleton = nullptr;
    if (!rig.SkeletonPath.empty())
    {
        Out.SkeletonPath = rig.SkeletonPath;
        if (Skeletons != nullptr)
            Out.Skeleton = Skeletons->Find(rig.SkeletonPath);
        skeleton = Out.Skeleton.IsValid() ? Skeletons->Get(Out.Skeleton) : nullptr;
        if (skeleton == nullptr)
        {
            // Only masks need the joints at bind; without them the rig still
            // plays, and says what it could not check.
            const std::string message = std::format("'{}' is not loaded, so no joint can be named against it.",
                                                    rig.SkeletonPath);
            if (masked)
                Error("anim.rig.skeleton_unavailable", Out.RigPath, "$.data.skeleton", message);
            else
                Warning("anim.rig.skeleton_unavailable", Out.RigPath, "$.data.skeleton", message);
        }
        else
            Out.JointCount = static_cast<std::uint32_t>(skeleton->Joints.size());
    }
    if (!masked)
        return;

    // A joint's name is its key; an ambiguous one cannot be named.
    std::unordered_map<std::string_view, int> byName;
    if (skeleton != nullptr)
        for (std::size_t j = 0; j < skeleton->Joints.size(); ++j)
        {
            const std::string& name = skeleton->Joints[j].Name;
            const auto [it, inserted] = byName.emplace(name, static_cast<int>(j));
            if (!inserted)
                it->second = -1;
        }

    for (std::size_t l = 0; l < rig.Layers.size() && l < Out.Layers.size(); ++l)
    {
        const std::vector<AnimMaskOp>& ops = rig.Layers[l].Mask;
        if (ops.empty())
            continue;
        const std::string field = std::format("$.data.layers[{}].mask", l);
        if (l == 0)
        {
            Error("anim.mask.first_layer", Out.RigPath, field,
                  "The first layer is the pose every other layer composes onto, so it covers every joint.");
            continue;
        }
        if (rig.SkeletonPath.empty())
        {
            Error("anim.mask.no_skeleton", Out.RigPath, field,
                  "A mask names joints, so the rig names the skeleton they belong to.");
            continue;
        }
        if (skeleton == nullptr)
            continue;

        const std::size_t joints = skeleton->Joints.size();
        std::vector<std::uint8_t> mask(joints, 0);
        std::vector<std::uint8_t> touched(joints, 0);
        bool ok = true;
        for (std::size_t m = 0; m < ops.size(); ++m)
        {
            const AnimMaskOp& op = ops[m];
            const auto found = byName.find(op.Joint);
            const std::string at = std::format("{}[{}].joint", field, m);
            if (found == byName.end())
            {
                Error("anim.mask.joint_unknown", Out.RigPath, at,
                      std::format("'{}' has no joint named '{}'.", rig.SkeletonPath, op.Joint));
                ok = false;
                continue;
            }
            if (found->second < 0)
            {
                Error("anim.mask.joint_ambiguous", Out.RigPath, at,
                      std::format("'{}' has more than one joint named '{}'; reimport it so every joint has "
                                  "its own name.",
                                  rig.SkeletonPath, op.Joint));
                ok = false;
                continue;
            }
            // Parents come before children, so one forward pass finds the
            // subtree: a joint is in it when its parent is.
            const std::size_t root = static_cast<std::size_t>(found->second);
            std::fill(touched.begin(), touched.end(), std::uint8_t{ 0 });
            touched[root] = 1;
            if (op.Subtree)
                for (std::size_t j = root + 1; j < joints; ++j)
                {
                    const std::int32_t parent = skeleton->Joints[j].ParentIndex;
                    touched[j] = parent >= 0 && touched[static_cast<std::size_t>(parent)] != 0 ? 1 : 0;
                }
            for (std::size_t j = 0; j < joints; ++j)
                if (touched[j] != 0)
                    mask[j] = op.Exclude ? 0 : 1;
        }
        if (!ok)
            continue;
        if (std::find(mask.begin(), mask.end(), std::uint8_t{ 1 }) == mask.end())
            Warning("anim.mask.empty", Out.RigPath, field, "The mask covers no joint, so the layer never shows.");
        Out.Layers[l].Mask = std::move(mask);
    }
}

void AnimRigBinder::ValidateClipSkeletons()
{
    if (!Out.Skeleton.IsValid() || Clips == nullptr)
        return;
    for (const AnimBoundContent& content : Out.Contents)
    {
        if (content.Flow >= 0 || !content.Clip.IsValid())
            continue;
        const SkeletonHandle skeleton = Clips->GetSkeleton(content.Clip);
        // A clip registered without a skeleton (a timing-only fixture) has
        // no joints to disagree about.
        if (skeleton.IsValid() && skeleton != Out.Skeleton)
            Error("anim.clip.skeleton_mismatch", content.Path, {},
                  std::format("It animates '{}', and the rig '{}' poses '{}'; its joints would land on the "
                              "wrong bones.",
                              Skeletons != nullptr ? std::string(Skeletons->GetName(skeleton)) : std::string("?"),
                              Out.RigPath, Out.SkeletonPath));
    }
}
