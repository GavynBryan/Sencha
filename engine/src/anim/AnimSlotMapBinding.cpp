#include "AnimRigBinder.h"

#include <anim/AnimSlotMapData.h>
#include <anim/AnimationClipCache.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace
{
    // What a clip's gameplay events commit an authority to: when, and through
    // which binding. Inputs may differ; timing may not.
    struct GameplayEventMark
    {
        // The flow section the mark plays in; 0 for a clip.
        std::size_t Section = 0;
        float Time = 0.0f;
        std::string Binding;
        bool operator==(const GameplayEventMark&) const = default;
    };

    void AppendMarks(const AnimationClipCache* clips, const AnimBoundContent& content, std::size_t section,
                     std::vector<GameplayEventMark>& marks)
    {
        const AnimationClipData* clip = clips != nullptr ? clips->Get(content.Clip) : nullptr;
        if (clip == nullptr)
            return;
        for (const AnimationClipEvent& event : clip->Events)
            if (event.Scope == AnimEventScope::Gameplay)
                marks.push_back({ section, event.Time, event.Binding });
    }

    // A flow's marks are its clip sections' in order. A slot section's are
    // its slot's own rows', which this check reaches through that behavior.
    std::vector<GameplayEventMark> GameplayEventMarks(const AnimationClipCache* clips, const AnimBoundRig& rig,
                                                      const AnimBoundContent& content)
    {
        std::vector<GameplayEventMark> marks;
        if (content.Flow < 0)
        {
            AppendMarks(clips, content, 0, marks);
            return marks;
        }
        const AnimBoundFlow& flow = rig.Flows[static_cast<std::size_t>(content.Flow)];
        for (std::size_t s = 0; s < flow.Sections.size(); ++s)
            if (flow.Sections[s].Content >= 0)
                AppendMarks(clips, rig.Contents[static_cast<std::size_t>(flow.Sections[s].Content)], s, marks);
        return marks;
    }
}

int AnimRigBinder::FindOrAddClipContent(const std::string& path)
{
    for (std::size_t i = 0; i < Out.Contents.size(); ++i)
    {
        if (Out.Contents[i].Path == path)
            return static_cast<int>(i);
    }
    const AnimationClipHandle clip = Clips != nullptr ? Clips->Find(path) : AnimationClipHandle{};
    const AnimationClipData* data = Clips != nullptr ? Clips->Get(clip) : nullptr;
    if (data == nullptr)
        return -1;
    AnimBoundContent content;
    content.Path = path;
    content.Clip = clip;
    content.DurationSeconds = data->DurationSeconds;
    Out.Contents.push_back(std::move(content));
    return static_cast<int>(Out.Contents.size() - 1);
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

            int content = -1;
            if (!decl.Flow.empty())
            {
                content = BindFlowContent(decl.Flow, path, at + ".flow");
                if (content < 0)
                    continue;
            }
            else if (!decl.Blendspace.empty())
            {
                content = BindBlendspaceContent(decl.Blendspace, path, at + ".blendspace");
                if (content < 0)
                    continue;
                // A mix's time is the path its coordinates took, which no
                // request records.
                if (const AnimBoundBehavior* policy = Out.FindBehavior(*behavior);
                    policy != nullptr
                    && (policy->Policy.Kind == AnimBehaviorKind::Flow || policy->Policy.RootMotion
                        || policy->Policy.LateJoin == AnimLateJoin::Reconstruct))
                    Error("anim.blendspace.behavior", path, at + ".blendspace",
                          std::format("'{}' is a flow, moves the character or reconstructs on late join; a "
                                      "blendspace's time depends on the path its facts took, so it plays only "
                                      "cyclic, one-shot or hold behaviors that do none of that.",
                                      decl.Behavior));
            }
            else
            {
                content = FindOrAddClipContent(decl.Clip);
                if (content < 0)
                {
                    Error("anim.slot.clip_unavailable", path, at + ".clip",
                          std::format("'{}' is not a loaded animation clip.", decl.Clip));
                    continue;
                }
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

    // Priority first, then stack order and row order, which is the append order.
    std::stable_sort(Out.SlotRows.begin(), Out.SlotRows.end(),
                     [](const AnimBoundSlotRow& a, const AnimBoundSlotRow& b) { return a.Priority > b.Priority; });

    // A row reading a local fact picks content per machine, so every candidate for its
    // behavior must take the same time and produce the same gameplay events, or machines
    // drift from the authority's timing.
    for (const AnimBoundSlotRow& row : Out.SlotRows)
    {
        if (!row.When.ReadsLocalFacts)
            continue;
        const std::string& path = Out.Contents[static_cast<std::size_t>(row.Content)].Path;
        const float duration = Out.Contents[static_cast<std::size_t>(row.Content)].DurationSeconds;
        const AnimBoundContent& content = Out.Contents[static_cast<std::size_t>(row.Content)];
        const std::vector<GameplayEventMark> events = GameplayEventMarks(Clips, Out, content);
        for (const AnimBoundSlotRow& other : Out.SlotRows)
        {
            if (other.Behavior != row.Behavior)
                continue;
            const AnimBoundContent& otherContent = Out.Contents[static_cast<std::size_t>(other.Content)];
            if (!content.IsClip() || !otherContent.IsClip())
            {
                if (content.Blendspace >= 0 || otherContent.Blendspace >= 0)
                {
                    if (row.Content != other.Content)
                    {
                        Error("anim.slot.local_timing", row.DeclaredIn, std::format("$.data.rows[{}].when", row.Index),
                              std::format("This row reads a local fact, so every row for '{}' must keep the same "
                                          "time; '{}' and '{}' do not, since a blendspace's time is its own.",
                                          row.BehaviorName, path, otherContent.Path));
                        break;
                    }
                    continue;
                }
                // Flows time by their sections: the same ones, running as long
                // and leaving the same way.
                const std::string difference = content.Flow < 0 || otherContent.Flow < 0
                    ? std::string("one plays a flow and the other a clip")
                    : AnimFlowAnchorDifference(Out, Out.Flows[static_cast<std::size_t>(content.Flow)],
                                               Out.Flows[static_cast<std::size_t>(otherContent.Flow)]);
                if (!difference.empty())
                {
                    Error("anim.slot.local_timing", row.DeclaredIn, std::format("$.data.rows[{}].when", row.Index),
                          std::format("This row reads a local fact, so every row for '{}' must keep the same time; "
                                      "'{}' and '{}' do not: {}.",
                                      row.BehaviorName, path, otherContent.Path, difference));
                    break;
                }
            }
            else if (std::abs(otherContent.DurationSeconds - duration) > 1e-4f)
            {
                Error("anim.slot.local_timing", row.DeclaredIn, std::format("$.data.rows[{}].when", row.Index),
                      std::format("This row reads a local fact, so every row for '{}' must last as long; "
                                  "'{}' takes {:.3f}s and '{}' {:.3f}s.",
                                  row.BehaviorName, path, duration, otherContent.Path,
                                  otherContent.DurationSeconds));
                break;
            }
            if (GameplayEventMarks(Clips, Out, otherContent) != events)
            {
                Error("anim.slot.local_events", row.DeclaredIn, std::format("$.data.rows[{}].when", row.Index),
                      std::format("This row reads a local fact, so every row for '{}' must produce the same "
                                  "gameplay events; '{}' and '{}' differ.",
                                  row.BehaviorName, path, otherContent.Path));
                break;
            }
        }
    }
}
