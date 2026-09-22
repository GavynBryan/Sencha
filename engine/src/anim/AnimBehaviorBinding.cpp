#include "AnimRigBinder.h"

#include <format>

void AnimRigBinder::BindBehaviors(const AnimRigData& rig)
{
    for (std::size_t s = 0; s < rig.BehaviorSetPaths.size(); ++s)
    {
        const std::string& path = rig.BehaviorSetPaths[s];
        const AnimBehaviorSet* set = Load<AnimBehaviorSet>(path, kAnimBehaviorSetType, Out.RigPath,
                                                           std::format("$.data.behaviors[{}]", s));
        if (set == nullptr)
            continue;
        for (std::size_t b = 0; b < set->Behaviors.size(); ++b)
        {
            const AnimBehaviorDecl& decl = set->Behaviors[b];
            const std::string at = std::format("$.data.behaviors[{}]", b);
            const std::optional<GameplayTagId> tag =
                ResolveTag(decl.Tag, path, at + ".tag", "anim.behavior.unresolved");
            if (!tag)
                continue;

            AnimBoundBehavior bound;
            bound.Tag = *tag;
            bound.Name = decl.Tag;
            bound.Policy = decl;
            bound.DeclaredIn = path;
            for (std::size_t t = 0; t < decl.Latch.Tags.size(); ++t)
            {
                if (std::optional<GameplayTagId> interrupt = ResolveTag(
                        decl.Latch.Tags[t], path, std::format("{}.latch.tags[{}]", at, t),
                        "anim.behavior.unresolved"))
                    bound.InterruptTags.push_back(*interrupt);
            }
            if (!decl.SyncGroup.empty())
            {
                if (std::optional<GameplayTagId> group =
                        ResolveTag(decl.SyncGroup, path, at + ".sync_group", "anim.behavior.unresolved"))
                    bound.SyncGroup = *group;
            }

            // A later set overrides an earlier one's policy for the same tag.
            if (const int existing = Out.FindBehaviorIndex(bound.Tag); existing >= 0)
                Out.Behaviors[static_cast<std::size_t>(existing)] = std::move(bound);
            else
                Out.Behaviors.push_back(std::move(bound));
        }
    }
}
