#include "AnimRigBinder.h"

#include <anim/AnimSlotMapData.h>
#include <anim/AnimationClipCache.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace
{
    int FindOrAddContent(AnimBoundRig& rig, const AnimationClipCache* clips, const std::string& path)
    {
        for (std::size_t i = 0; i < rig.Contents.size(); ++i)
        {
            if (rig.Contents[i].Path == path)
                return static_cast<int>(i);
        }
        const AnimationClipHandle clip = clips != nullptr ? clips->Find(path) : AnimationClipHandle{};
        const AnimationClipData* data = clips != nullptr ? clips->Get(clip) : nullptr;
        if (data == nullptr)
            return -1;
        rig.Contents.push_back({ path, clip, data->DurationSeconds });
        return static_cast<int>(rig.Contents.size() - 1);
    }
}

void AnimRigBinder::BindSlotMaps(const AnimRigData& rig)
{
    for (std::size_t m = 0; m < rig.SlotMapPaths.size(); ++m)
    {
        const std::string& path = rig.SlotMapPaths[m];
        const AnimSlotMapData* map =
            Load<AnimSlotMapData>(path, kAnimSlotMapType, Out.RigPath, std::format("$.data.slot_maps[{}]", m));
        if (map == nullptr)
            continue;
        for (std::size_t r = 0; r < map->Rows.size(); ++r)
        {
            const AnimSlotRowDecl& decl = map->Rows[r];
            const std::string at = std::format("$.data.rows[{}]", r);
            const std::optional<GameplayTagId> behavior =
                ResolveTag(decl.Behavior, path, at + ".behavior", "anim.slot.behavior_unresolved");
            if (!behavior)
                continue;
            if (Out.FindBehavior(*behavior) == nullptr)
                Warning("anim.slot.undeclared_behavior", path, at + ".behavior",
                        std::format("'{}' has no policy in the rig's behavior sets; it plays as a "
                                    "cyclic behavior with no latch.",
                                    decl.Behavior));

            std::vector<AnimDiagnostic> problems;
            AnimProgram when = CompileAnimPredicate(decl.When, Out, Tags(), path, at + ".when", problems);
            for (AnimDiagnostic& problem : problems)
                Report(problem.Severity, std::move(problem.Code), std::move(problem.AssetPath),
                       std::move(problem.FieldPath), std::move(problem.Message));

            const int content = FindOrAddContent(Out, Clips, decl.Clip);
            if (content < 0)
            {
                Error("anim.slot.clip_unavailable", path, at + ".clip",
                      std::format("'{}' is not a loaded animation clip.", decl.Clip));
                continue;
            }

            AnimBoundSlotRow row;
            row.Behavior = *behavior;
            row.BehaviorName = decl.Behavior;
            row.Priority = decl.Priority;
            row.When = std::move(when);
            row.Content = content;
            row.DeclaredIn = path;
            row.Index = static_cast<std::uint32_t>(r);
            row.Key = AnimStableKey(std::format("{}#{}", path, r));
            Out.SlotRows.push_back(std::move(row));
        }
    }

    // Priority first; within a priority the stack order, then row order,
    // which is the order rows were appended in.
    std::stable_sort(Out.SlotRows.begin(), Out.SlotRows.end(),
                     [](const AnimBoundSlotRow& a, const AnimBoundSlotRow& b) { return a.Priority > b.Priority; });

    // A row that reads a local fact picks content per machine, so every
    // candidate for its behavior must take the same time: otherwise what one
    // machine chose would drift from what the others are timing against.
    for (const AnimBoundSlotRow& row : Out.SlotRows)
    {
        if (!row.When.ReadsLocalFacts)
            continue;
        const float duration = Out.Contents[static_cast<std::size_t>(row.Content)].DurationSeconds;
        for (const AnimBoundSlotRow& other : Out.SlotRows)
        {
            if (other.Behavior != row.Behavior)
                continue;
            const float otherDuration = Out.Contents[static_cast<std::size_t>(other.Content)].DurationSeconds;
            if (std::abs(otherDuration - duration) > 1e-4f)
            {
                Error("anim.slot.local_timing", row.DeclaredIn, std::format("$.data.rows[{}].when", row.Index),
                      std::format("This row reads a local fact, so every row for '{}' must last as long; "
                                  "'{}' takes {:.3f}s and '{}' {:.3f}s.",
                                  row.BehaviorName, Out.Contents[static_cast<std::size_t>(row.Content)].Path,
                                  duration, Out.Contents[static_cast<std::size_t>(other.Content)].Path,
                                  otherDuration));
                break;
            }
        }
    }
}
