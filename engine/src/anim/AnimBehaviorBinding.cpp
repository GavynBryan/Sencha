#include "AnimRigBinder.h"

#include <anim/AnimBlendOverrides.h>
#include <ecs/World.h>

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

void AnimRigBinder::BindBlendOverrides(const AnimRigData& rig)
{
    for (const AnimBoundBehavior& behavior : Out.Behaviors)
        if (behavior.Policy.Blend.Phase == AnimPhasePolicy::Carry && !behavior.SyncGroup.IsValid())
            Warning("anim.blend.carry_without_group", behavior.DeclaredIn, "$.data.behaviors",
                    std::format("'{}' carries phase but names no sync group, so it always starts over.",
                                behavior.Name));

    for (std::size_t a = 0; a < rig.BlendOverridePaths.size(); ++a)
    {
        const std::string& path = rig.BlendOverridePaths[a];
        const AnimBlendOverrides* overrides = Load<AnimBlendOverrides>(
            path, kAnimBlendOverridesType, Out.RigPath, std::format("$.data.blend_overrides[{}]", a));
        if (overrides == nullptr)
            continue;
        for (std::size_t i = 0; i < overrides->Overrides.size(); ++i)
        {
            const AnimBlendOverrideDecl& decl = overrides->Overrides[i];
            const std::string at = std::format("$.data.overrides[{}]", i);
            AnimBoundBlendOverride bound;
            bool ok = true;
            for (const auto& [name, key, out] : { std::tuple{ &decl.From, "from", &bound.From },
                                                  std::tuple{ &decl.To, "to", &bound.To } })
            {
                const std::optional<GameplayTagId> tag =
                    ResolveTag(*name, path, std::format("{}.{}", at, key), "anim.blend.unresolved");
                if (tag && Out.FindBehaviorIndex(*tag) < 0)
                {
                    Error("anim.blend.undeclared_behavior", path, std::format("{}.{}", at, key),
                          std::format("'{}' is not declared by any of the rig's behavior sets.", *name));
                    ok = false;
                }
                if (!tag)
                    ok = false;
                else
                    *out = *tag;
            }
            if (!ok)
                continue;
            bound.Policy = decl.Blend;
            bound.DeclaredIn = path;
            bound.Index = static_cast<std::uint32_t>(i);
            const auto existing = std::ranges::find_if(Out.BlendOverrides, [&](const AnimBoundBlendOverride& other) {
                return other.From == bound.From && other.To == bound.To;
            });
            if (existing != Out.BlendOverrides.end())
                *existing = std::move(bound);
            else
                Out.BlendOverrides.push_back(std::move(bound));
        }
    }

    // Rare by construction: past half the cap is a warning, past the cap an
    // error the cvar can raise.
    const AnimRigLimits* limits = WorldRef.TryGetResource<AnimRigLimits>();
    const std::uint32_t cap = limits != nullptr ? limits->BlendOverrideCap : AnimRigLimits{}.BlendOverrideCap;
    const std::size_t count = Out.BlendOverrides.size();
    if (count > cap)
        Error("anim.blend.override_cap", Out.RigPath, "$.data.blend_overrides",
              std::format("{} blend overrides bind here and the cap is {} (anim.blend.override_cap). A rig "
                          "that needs this many wants another behavior or a fact.",
                          count, cap));
    else if (count > cap / 2)
        Warning("anim.blend.override_count", Out.RigPath, "$.data.blend_overrides",
                std::format("{} of at most {} blend overrides: each is a pairwise exception to the "
                            "destination's policy.",
                            count, cap));
}
