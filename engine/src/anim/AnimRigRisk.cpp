#include <anim/AnimRigRisk.h>

#include <anim/AnimRigBinding.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <format>

namespace
{
    std::string NameOf(const GameplayTagRegistry* tags, GameplayTagId tag)
    {
        return tags != nullptr && tag.IsValid() ? std::string(tags->GetName(tag)) : std::format("tag {}", tag.Value);
    }
}

AnimRigRisk MeasureAnimRigRisk(const AnimBoundRig& rig, const GameplayTagRegistry* tags)
{
    AnimRigRisk risk;
    risk.BlendOverrides = static_cast<std::uint32_t>(rig.BlendOverrides.size());
    if (risk.BlendOverrides > kAnimRiskBlendOverrides)
        risk.Findings.push_back({ "anim.risk.blend_overrides",
                                  std::format("{} pairwise blend overrides; a rig needing many wants a new behavior "
                                              "or a fact instead.",
                                              risk.BlendOverrides) });

    for (const AnimBoundSelector& selector : rig.Selectors)
    {
        const auto rules = static_cast<std::uint32_t>(selector.Rules.size());
        risk.SelectorRules += rules;
        risk.DeepestSelector = std::max(risk.DeepestSelector, rules);
    }

    for (const AnimBoundFlow& flow : rig.Flows)
        if (flow.Sections.size() > kAnimRiskFlowSections)
        {
            ++risk.LongFlows;
            risk.Findings.push_back({ "anim.risk.long_flow",
                                      std::format("{} has {} sections; past {} a flow is becoming a state machine.",
                                                  flow.Path, flow.Sections.size(), kAnimRiskFlowSections) });
        }

    // Who can select each behavior: a rule that needs no request (a fact), a
    // rule that does, or a request-keyed layer playing its intent.
    const bool requestKeyed =
        std::ranges::any_of(rig.Layers, [](const AnimBoundLayer& layer) { return layer.Selector < 0; });
    for (const AnimBoundBehavior& behavior : rig.Behaviors)
    {
        bool byFact = false;
        bool byRequest = requestKeyed && rig.FindIntent(behavior.Tag) != nullptr;
        for (const AnimBoundSelector& selector : rig.Selectors)
            for (const AnimBoundRule& rule : selector.Rules)
                if (rule.Behavior == behavior.Tag)
                    (rule.RequiresRequest ? byRequest : byFact) = true;
        if (byFact && byRequest)
            risk.Findings.push_back({ "anim.risk.fact_and_request",
                                      std::format("{} is selected both by a fact and by a request; one path should "
                                                  "own it.",
                                                  behavior.Name) });
    }

    // An intent is played when a request-keyed layer has a row for it, or a
    // rule reads it.
    for (const AnimBoundIntent& intent : rig.Intents)
    {
        bool claimed = requestKeyed
            && std::ranges::any_of(rig.SlotRows, [&](const AnimBoundSlotRow& row) { return row.Behavior == intent.Intent; });
        for (const AnimBoundSelector& selector : rig.Selectors)
            for (const AnimBoundRule& rule : selector.Rules)
                claimed = claimed || std::ranges::find(rule.Enter.Intents, intent.Intent) != rule.Enter.Intents.end()
                       || std::ranges::find(rule.Stay.Intents, intent.Intent) != rule.Stay.Intents.end();
        if (!claimed)
            risk.Findings.push_back({ "anim.risk.unclaimed_intent",
                                      std::format("{} is declared but nothing plays it; a request for it does "
                                                  "nothing.",
                                                  NameOf(tags, intent.Intent)) });
    }
    return risk;
}
