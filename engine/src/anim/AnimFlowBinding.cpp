#include "AnimRigBinder.h"

#include <anim/AnimFlowData.h>
#include <anim/AnimSlotMapData.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <format>
#include <optional>

int AnimRigBinder::BindFlowContent(const std::string& path, const std::string& referrer, const std::string& field)
{
    for (std::size_t i = 0; i < Out.Contents.size(); ++i)
        if (Out.Contents[i].Path == path && Out.Contents[i].Flow >= 0)
            return static_cast<int>(i);

    const AnimFlowData* flow = Load<AnimFlowData>(path, kAnimFlowType, referrer, field);
    if (flow == nullptr)
        return -1;

    AnimBoundFlow bound;
    bound.Path = path;
    bool ok = true;
    for (std::size_t s = 0; s < flow->Sections.size(); ++s)
    {
        const AnimFlowSectionDecl& decl = flow->Sections[s];
        const std::string at = std::format("$.data.sections[{}]", s);
        AnimBoundFlowSection section;
        section.TagName = decl.Tag;
        if (const std::optional<GameplayTagId> tag = ResolveTag(decl.Tag, path, at + ".tag", "anim.flow.tag_unresolved"))
            section.Tag = *tag;
        else
            ok = false;

        if (!decl.Clip.empty())
        {
            section.Content = FindOrAddClipContent(decl.Clip);
            if (section.Content < 0)
            {
                Error("anim.flow.clip_unavailable", path, at + ".clip",
                      std::format("'{}' is not a loaded animation clip.", decl.Clip));
                ok = false;
            }
        }
        else if (const std::optional<GameplayTagId> slot =
                     ResolveTag(decl.Slot, path, at + ".slot", "anim.flow.slot_unresolved"))
            section.Slot = *slot;
        else
            ok = false;

        const auto compile = [&](const AnimPredicateDecl& predicate, const std::string& where) {
            std::vector<AnimDiagnostic> problems;
            AnimProgram program = CompileAnimPredicate(predicate, Out, Tags(), path, where, problems);
            for (AnimDiagnostic& problem : problems)
            {
                ok = ok && problem.Severity != AnimDiagnosticSeverity::Error;
                Report(problem.Severity, std::move(problem.Code), std::move(problem.AssetPath),
                       std::move(problem.FieldPath), std::move(problem.Message));
            }
            return program;
        };
        section.Loop = decl.Loop;
        if (decl.Loop == AnimFlowLoop::While)
            section.While = compile(decl.While, at + ".while");
        if (decl.Loop == AnimFlowLoop::Count)
        {
            const std::optional<GameplayTagId> intent =
                ResolveTag(decl.CountIntent, path, at + ".count_intent", "anim.flow.count_unresolved");
            const AnimBoundIntent* boundIntent = intent ? Out.FindIntent(*intent) : nullptr;
            if (boundIntent != nullptr)
            {
                for (std::size_t p = 0; p < boundIntent->Params.size(); ++p)
                    if (boundIntent->Params[p].Name == decl.CountParam)
                        section.CountParam = static_cast<int>(p);
                if (section.CountParam >= 0 && boundIntent->Params[static_cast<std::size_t>(section.CountParam)].Kind
                        != AnimRequestParamKind::Int)
                {
                    Error("anim.flow.count_param", path, at + ".count_param",
                          std::format("'{}' of '{}' is not an int parameter, so it cannot count loops.",
                                      decl.CountParam, decl.CountIntent));
                    ok = false;
                }
            }
            if (boundIntent == nullptr || section.CountParam < 0)
            {
                Error("anim.flow.count_param", path, at + ".count_param",
                      std::format("The rig's request schema declares no parameter '{}' on '{}'.", decl.CountParam,
                                  decl.CountIntent));
                ok = false;
            }
            section.CountIntent = intent.value_or(GameplayTagId{});
        }
        for (std::size_t b = 0; b < decl.Branches.size(); ++b)
        {
            AnimBoundFlowBranch branch;
            branch.When = compile(decl.Branches[b].When, std::format("{}.branches[{}].when", at, b));
            branch.To = static_cast<std::uint8_t>(flow->FindSection(decl.Branches[b].To));
            section.Branches.push_back(std::move(branch));
        }
        section.Ends = decl.Ends;
        section.CancelTiming = decl.CancelTiming;
        bound.NeedsRequest = bound.NeedsRequest || decl.Loop != AnimFlowLoop::Once
            || decl.CancelTiming == AnimCancelTiming::Immediate;
        bound.Sections.push_back(std::move(section));
    }
    bound.Cancel = flow->Cancel.empty() ? -1 : flow->FindSection(flow->Cancel);
    bound.SectionEnteredDecl = flow->SectionEntered;
    bound.SectionExitedDecl = flow->SectionExited;
    if (!ok)
        return -1;

    Out.Flows.push_back(std::move(bound));
    AnimBoundContent content;
    content.Path = path;
    content.Flow = static_cast<int>(Out.Flows.size() - 1);
    Out.Contents.push_back(std::move(content));
    return static_cast<int>(Out.Contents.size() - 1);
}

namespace
{
    bool SameProgram(const AnimProgram& a, const AnimProgram& b)
    {
        const auto sameOp = [](const AnimOpcode& x, const AnimOpcode& y) {
            return x.Op == y.Op && x.Mode == y.Mode && x.Operand == y.Operand && x.Index == y.Index
                && x.Constant == y.Constant;
        };
        return std::ranges::equal(a.Ops, b.Ops, sameOp) && a.RowEnds == b.RowEnds && a.Queries == b.Queries;
    }

    // The length a section plays for, or none when a slot section can
    // resolve to clips of different lengths.
    std::optional<float> SectionDuration(const AnimBoundRig& rig, const AnimBoundFlowSection& section)
    {
        if (section.Content >= 0)
            return rig.Contents[static_cast<std::size_t>(section.Content)].DurationSeconds;
        std::optional<float> duration;
        for (const AnimBoundSlotRow& row : rig.SlotRows)
        {
            if (row.Behavior != section.Slot || row.Content < 0)
                continue;
            const float length = rig.Contents[static_cast<std::size_t>(row.Content)].DurationSeconds;
            if (duration.has_value() && *duration != length)
                return std::nullopt;
            duration = length;
        }
        return duration.value_or(0.0f);
    }
}

// A request's anchor is a section index and the tick it began, so every flow
// it anchors must have the same sections running for the same time and
// leaving them the same way.
std::string AnimFlowAnchorDifference(const AnimBoundRig& rig, const AnimBoundFlow& a, const AnimBoundFlow& b)
{
    if (a.Sections.size() != b.Sections.size())
        return std::format("they have {} and {} sections", a.Sections.size(), b.Sections.size());
    if (a.Cancel != b.Cancel)
        return "their cancel sections differ";
    for (std::size_t s = 0; s < a.Sections.size(); ++s)
    {
        const AnimBoundFlowSection& x = a.Sections[s];
        const AnimBoundFlowSection& y = b.Sections[s];
        // The same clip, or the same slot resolved over the same facts, runs
        // as long in both.
        if (x.Content != y.Content || x.Slot != y.Slot)
        {
            const std::optional<float> dx = SectionDuration(rig, x);
            const std::optional<float> dy = SectionDuration(rig, y);
            if (!dx || !dy)
                return std::format("section {} resolves a slot to clips of different lengths", s);
            if (*dx != *dy)
                return std::format("section {} runs {} s in one and {} s in the other", s, *dx, *dy);
        }
        const bool sameLoop = x.Loop == y.Loop
            && (x.Loop != AnimFlowLoop::While || SameProgram(x.While, y.While))
            && (x.Loop != AnimFlowLoop::Count || (x.CountIntent == y.CountIntent && x.CountParam == y.CountParam));
        if (!sameLoop)
            return std::format("section {} loops differently", s);
        const bool sameBranches = std::ranges::equal(x.Branches, y.Branches,
            [](const AnimBoundFlowBranch& p, const AnimBoundFlowBranch& q) {
                return p.To == q.To && SameProgram(p.When, q.When);
            });
        if (!sameBranches || x.Ends != y.Ends || x.CancelTiming != y.CancelTiming)
            return std::format("section {} leaves differently", s);
    }
    return {};
}

void AnimRigBinder::ValidateFlows()
{
    for (const AnimBoundSlotRow& row : Out.SlotRows)
    {
        const AnimBoundContent& content = Out.Contents[static_cast<std::size_t>(row.Content)];
        if (content.Flow < 0)
            continue;
        const AnimBoundFlow& flow = Out.Flows[static_cast<std::size_t>(content.Flow)];
        const std::string field = std::format("$.data.rows[{}].flow", row.Index);

        const AnimBoundBehavior* behavior = Out.FindBehavior(row.Behavior);
        if (behavior != nullptr && behavior->Policy.Kind != AnimBehaviorKind::Flow)
            Error("anim.flow.behavior_kind", row.DeclaredIn, field,
                  std::format("'{}' plays a flow here, so its behavior kind is flow.", row.BehaviorName));

        // Slot sections resolve through this rig's slot map, and must find
        // clips there: a flow does not nest.
        for (const AnimBoundFlowSection& section : flow.Sections)
        {
            if (!section.Slot.IsValid())
                continue;
            for (const AnimBoundSlotRow& slotRow : Out.SlotRows)
                if (slotRow.Behavior == section.Slot && !Out.Contents[static_cast<std::size_t>(slotRow.Content)].IsClip())
                    Error("anim.flow.nested", flow.Path, "$.data.sections",
                          std::format("Section '{}' resolves '{}' to a flow or blendspace; a section plays a clip.",
                                      section.TagName, slotRow.BehaviorName));
        }

        ValidateSharedAnchor(row);

        if (!flow.NeedsRequest)
            continue;
        // Loops and immediate cancels are only reconstructible from a request's
        // anchor, so every way this behavior can win must run through one.
        for (std::size_t l = 0; l < Out.Layers.size(); ++l)
        {
            const AnimBoundLayer& layer = Out.Layers[l];
            if (layer.Idle == row.Behavior)
                Error("anim.flow.cosmetic_loop", row.DeclaredIn, field,
                      std::format("'{}' is layer {}'s idle, played without a request, and its flow loops or "
                                  "cancels immediately.",
                                  row.BehaviorName, layer.NameText));
            if (layer.Selector < 0)
                continue;
            for (const AnimBoundRule& rule : Out.Selectors[static_cast<std::size_t>(layer.Selector)].Rules)
                if (rule.Behavior == row.Behavior && !rule.RequiresRequest)
                    Error("anim.flow.cosmetic_loop", row.DeclaredIn, field,
                          std::format("'{}' is reached by rule '{}' without a request, and its flow loops or "
                                      "cancels immediately; only a request's anchor lets a late joiner find the "
                                      "section.",
                                      row.BehaviorName, rule.Label));
        }
    }
}

void AnimRigBinder::ValidateSharedAnchor(const AnimBoundSlotRow& row)
{
    // The intents that drive `behavior` on layer `l`: a request-keyed layer
    // plays the intent as the behavior, a selector layer the behaviors of the
    // rules that latch on it.
    const auto drivenBy = [&](std::size_t l, GameplayTagId behavior) {
        std::vector<GameplayTagId> intents;
        const AnimBoundLayer& layer = Out.Layers[l];
        if (layer.Selector < 0)
        {
            if (Out.FindIntent(behavior) != nullptr)
                intents.push_back(behavior);
            return intents;
        }
        for (const AnimBoundRule& rule : Out.Selectors[static_cast<std::size_t>(layer.Selector)].Rules)
            if (rule.Behavior == behavior && rule.LatchIntent.IsValid())
                intents.push_back(rule.LatchIntent);
        return intents;
    };

    const AnimBoundFlow& flow = Out.Flows[static_cast<std::size_t>(Out.Contents[static_cast<std::size_t>(row.Content)].Flow)];
    for (std::size_t l = 0; l < Out.Layers.size(); ++l)
    {
        const std::vector<GameplayTagId> intents = drivenBy(l, row.Behavior);
        for (std::size_t m = 0; m < Out.Layers.size(); ++m)
        {
            if (m == l)
                continue;
            // Each pair of rows once, in both layer orders.
            for (const AnimBoundSlotRow& other : Out.SlotRows)
            {
                if (&other < &row || other.Content < 0
                    || Out.Contents[static_cast<std::size_t>(other.Content)].Flow < 0)
                    continue;
                const std::vector<GameplayTagId> otherIntents = drivenBy(m, other.Behavior);
                const auto shared = std::ranges::find_first_of(intents, otherIntents);
                if (shared == intents.end())
                    continue;
                const AnimBoundFlow& otherFlow =
                    Out.Flows[static_cast<std::size_t>(Out.Contents[static_cast<std::size_t>(other.Content)].Flow)];
                const std::string difference = AnimFlowAnchorDifference(Out, flow, otherFlow);
                if (difference.empty())
                    continue;
                const GameplayTagRegistry* tags = Tags();
                Error("anim.flow.anchor_timing", other.DeclaredIn, std::format("$.data.rows[{}].flow", other.Index),
                      std::format("One '{}' request plays '{}' on layer {} and '{}' on layer {}, and a request "
                                  "carries one anchor; {}.",
                                  tags != nullptr ? std::string(tags->GetName(*shared)) : std::string("?"), flow.Path,
                                  Out.Layers[l].NameText, otherFlow.Path, Out.Layers[m].NameText, difference));
            }
        }
    }
}
