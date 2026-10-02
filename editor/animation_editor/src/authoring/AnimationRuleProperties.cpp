#include "authoring/AnimationRuleProperties.h"

#include "authoring/AnimationLayerChoice.h"
#include "authoring/AnimationPredicateText.h"
#include "authoring/AnimationPreviewWorkspace.h"

#include "properties/PropertyTree.h"
#include "properties/SettingProperties.h"

#include <anim/AnimBehaviorSet.h>
#include <anim/AnimDecisionLog.h>
#include <anim/AnimationClip.h>

#include <algorithm>
#include <format>
#include <optional>
#include <tuple>

namespace
{
constexpr std::string_view kRules = "rules.";
constexpr std::string_view kSlots = "slots.";
constexpr std::string_view kSelectedBy = "selected_by.";
constexpr std::size_t kLogShown = 40;

std::string ContentPath(const AnimBoundRig& rig, int content)
{
    return content >= 0 && static_cast<std::size_t>(content) < rig.Contents.size()
               ? rig.Contents[static_cast<std::size_t>(content)].Path
               : std::string("(no content)");
}

const AnimBoundSelector* SelectorOf(const AnimBoundRig& rig, std::size_t layer)
{
    const int selector = layer < rig.Layers.size() ? rig.Layers[layer].Selector : -1;
    return selector >= 0 ? &rig.Selectors[static_cast<std::size_t>(selector)] : nullptr;
}

const AnimBoundRule* PickedRule(const AnimBoundRig& rig, const AnimationNavigation& navigation)
{
    const AnimBoundSelector* selector = SelectorOf(rig, navigation.Layer);
    return selector != nullptr && navigation.Rule >= 0
                   && static_cast<std::size_t>(navigation.Rule) < selector->Rules.size()
               ? &selector->Rules[static_cast<std::size_t>(navigation.Rule)]
               : nullptr;
}

// The index after `prefix` in `key`, or nothing.
std::string Milliseconds(float ms)
{
    return std::format("{:g} ms", ms);
}
} // namespace

std::string AnimationRuleProperties::Title() const
{
    const AnimBoundRig* rig = Workspace.Rig.Simulation.Rig();
    if (rig == nullptr || rig->Layers.empty())
        return "Rules";
    if (const AnimBoundRule* rule = PickedRule(*rig, Workspace.Rig.Navigation))
        return rule->Label;
    return "Rules of " + rig->Layers[std::min(Workspace.Rig.Navigation.Layer, rig->Layers.size() - 1)].NameText;
}

std::vector<PropertyNode> AnimationRuleProperties::Nodes() const
{
    const AnimationPreviewSession& session = Workspace.Rig.Simulation;
    const AnimBoundRig* rig = session.Rig();
    if (rig == nullptr || rig->Layers.empty())
        return { NoteProperty("none", "Rules", "Open a rig to see its rules decide.") };
    const AnimationNavigation& navigation = Workspace.Rig.Navigation;
    const std::size_t layer = std::min(navigation.Layer, rig->Layers.size() - 1);

    std::vector<PropertyNode> nodes{ AnimationLayerChoice(*rig, layer) };
    const AnimBoundSelector* selector = SelectorOf(*rig, layer);
    if (selector == nullptr)
    {
        nodes.push_back(NoteProperty("chosen_by", "Chosen by",
                                     std::format("Requests: the newest claiming the layer names its behavior; "
                                                 "otherwise it plays {}.",
                                                 session.TagName(rig->Layers[layer].Idle))));
        return nodes;
    }
    PropertyNode next = ToggleProperty("next", "Next tick", ShowNext && !session.IsPlaying(),
                                       "While paused, how the rules would fare on the tick to come");
    next.Descriptor.ReadOnly = session.IsPlaying();
    nodes.push_back(std::move(next));
    if (PickedRule(*rig, navigation) != nullptr)
    {
        const std::vector<PropertyNode> rule = RuleNodes();
        nodes.insert(nodes.end(), rule.begin(), rule.end());
        return nodes;
    }

    const std::vector<AnimRuleVerdict> verdicts = Verdicts(layer);
    const DataAssetCache& data = Workspace.DataCache();
    PropertyNode rules = GroupProperty("rules", "Rules");
    for (std::size_t i = 0; i < selector->Rules.size(); ++i)
    {
        const AnimBoundRule& rule = selector->Rules[i];
        PropertyNode row = NoteProperty(std::format("{}{}", kRules, i), rule.Label,
                                        i < verdicts.size() ? DescribeAnimVerdict(data, rule, verdicts[i])
                                                            : std::string("not yet decided"));
        row.Descriptor.Verbs = { { .Id = "pick", .Label = "Inspect" } };
        rules.Children.push_back(std::move(row));
    }
    nodes.push_back(std::move(rules));
    return nodes;
}

std::vector<AnimRuleVerdict> AnimationRuleProperties::Verdicts(std::size_t layer) const
{
    AnimationPreviewSession& session = Workspace.Rig.Simulation;
    if (ShowNext && !session.IsPlaying())
    {
        std::vector<std::vector<AnimRuleVerdict>> next = session.ExplainNextTick();
        return layer < next.size() ? std::move(next[layer]) : std::vector<AnimRuleVerdict>{};
    }
    const AnimationPreviewTickRecord* record = Workspace.Rig.Inspected();
    return record != nullptr && layer < record->Layers.size() ? record->Layers[layer].Verdicts
                                                              : std::vector<AnimRuleVerdict>{};
}

std::vector<PropertyNode> AnimationRuleProperties::RuleNodes() const
{
    const AnimationPreviewSession& session = Workspace.Rig.Simulation;
    const AnimBoundRig& rig = *session.Rig();
    const AnimationNavigation& navigation = Workspace.Rig.Navigation;
    const AnimBoundRule& rule = *PickedRule(rig, navigation);
    const DataAssetCache& data = Workspace.DataCache();
    const std::vector<AnimRuleVerdict> verdicts = Verdicts(navigation.Layer);
    const std::size_t index = static_cast<std::size_t>(navigation.Rule);

    std::vector<PropertyNode> nodes;
    nodes.push_back(NoteProperty("verdict", ShowNext && !session.IsPlaying() ? "Next tick" : "This tick",
                                 index < verdicts.size() ? DescribeAnimVerdict(data, rule, verdicts[index])
                                                         : std::string("not yet decided")));
    nodes.push_back(NoteProperty("band", "Priority", std::to_string(rule.PriorityBand)));
    nodes.push_back(NoteProperty("enter", "Enter", DescribeAnimRuleRows(data, rule.EnterRows)));
    nodes.push_back(NoteProperty("stay", "Stay", rule.HasStay ? DescribeAnimRuleRows(data, rule.StayRows)
                                                              : std::string("as enter")));
    nodes.push_back(NoteProperty("hold", "Hold, cooldown",
                                 std::format("{}, {}", Milliseconds(rule.HoldMinMs), Milliseconds(rule.CooldownMs))));
    std::string chain;
    for (const AnimRuleSource& source : rule.Source)
        chain += std::format("{}{} rule {}", chain.empty() ? "" : ", through ", source.Selector, source.Name);
    PropertyNode authored = NoteProperty("source", "Authored in", chain);
    if (!rule.Source.empty())
        authored.Descriptor.Verbs = { { .Id = "open", .Label = "Edit" } };
    nodes.push_back(std::move(authored));
    const std::vector<PropertyNode> behavior = BehaviorNodes();
    nodes.insert(nodes.end(), behavior.begin(), behavior.end());
    return nodes;
}

std::vector<PropertyNode> AnimationRuleProperties::BehaviorNodes() const
{
    const AnimationPreviewSession& session = Workspace.Rig.Simulation;
    const AnimBoundRig& rig = *session.Rig();
    const AnimationNavigation& navigation = Workspace.Rig.Navigation;
    const GameplayTagId tag = PickedRule(rig, navigation)->Behavior;

    std::vector<PropertyNode> nodes;
    PropertyNode policy = GroupProperty("behavior", "Behavior " + session.TagName(tag));
    if (const AnimBoundBehavior* behavior = rig.FindBehavior(tag))
    {
        const AnimBehaviorDecl& decl = behavior->Policy;
        constexpr std::string_view kBlends[] = { "inertialize", "crossfade", "snap" };
        constexpr std::string_view kLateJoins[] = { "skip", "snap to end", "reconstruct" };
        std::string latch(AnimLatchModeName(decl.Latch.Mode));
        if (decl.Latch.Mode != AnimLatchMode::None)
        {
            if (decl.Latch.InterruptibleBy == AnimInterruptKind::PriorityAtLeast)
                latch += std::format(", interrupted from priority {}", decl.Latch.Priority);
            else if (decl.Latch.InterruptibleBy == AnimInterruptKind::Tags)
                for (const std::string& interrupt : decl.Latch.Tags)
                    latch += ", interrupted by " + interrupt;
            else
                latch += ", never interrupted";
        }
        policy.Children = {
            NoteProperty("behavior.kind", "Kind", std::string(AnimBehaviorKindName(decl.Kind))),
            NoteProperty("behavior.blend", "Blend in",
                         std::format("{} {:g} ms{}", kBlends[static_cast<int>(decl.Blend.In)], decl.Blend.InMs,
                                     decl.Blend.Phase == AnimPhasePolicy::Carry ? ", phase carried" : "")),
            NoteProperty("behavior.latch", "Latch", latch),
            NoteProperty("behavior.late_join", "Late join",
                         std::format("{}{}", kLateJoins[static_cast<int>(decl.LateJoin)],
                                     decl.RootMotion ? ", root motion" : "")),
            NoteProperty("behavior.event_weight", "Event weight",
                         std::format("Cosmetic events below {:.2f} are suppressed", decl.EventWeight)),
        };
        for (const auto& [key, label, bound] : { std::tuple{ "behavior.entered", "On entered", &behavior->Entered },
                                                 std::tuple{ "behavior.exited", "On exited", &behavior->Exited } })
            policy.Children.push_back(NoteProperty(
                key, label,
                bound->has_value() ? std::format("{} ({}){}", (*bound)->BindingText,
                                                 AnimEventScopeName((*bound)->Scope),
                                                 (*bound)->Resolved ? "" : ", unresolved")
                                   : std::string("nothing")));
        PropertyNode declared = NoteProperty("behavior.declared", "Declared in", behavior->DeclaredIn);
        declared.Descriptor.Verbs = { { .Id = "open", .Label = "Edit" } };
        policy.Children.push_back(std::move(declared));
    }
    else
        policy.Children.push_back(NoteProperty("behavior.none", "Policy",
                                               "No behavior set declares one: it plays as cyclic content with no "
                                               "latch."));
    nodes.push_back(std::move(policy));

    const AnimContentState* playing = session.Content();
    PropertyNode slots = GroupProperty("slots", "Resolved by");
    for (std::size_t r = 0; r < rig.SlotRows.size(); ++r)
    {
        const AnimBoundSlotRow& row = rig.SlotRows[r];
        if (row.Behavior != tag)
            continue;
        std::string where = std::format("row {} of {}", row.Index, row.DeclaredIn);
        if (playing != nullptr)
            for (std::size_t l = 0; l < rig.Layers.size() && l < kAnimMaxLayers; ++l)
                if (playing->Layers[l].Row == r)
                    where += std::format(", playing on {} at {:.2f}s", rig.Layers[l].NameText,
                                         playing->Layers[l].TimeSeconds);
        PropertyNode slot = NoteProperty(std::format("{}{}", kSlots, r), ContentPath(rig, row.Content), where);
        slot.Descriptor.Verbs = { { .Id = "audition", .Label = "Audition" }, { .Id = "open", .Label = "Edit" } };
        slots.Children.push_back(std::move(slot));
    }
    if (slots.Children.empty())
        slots.Children.push_back(NoteProperty("slots.none", "Content", "No slot row resolves this behavior."));
    nodes.push_back(std::move(slots));

    PropertyNode others = GroupProperty("selected_by", "Also selected by");
    for (std::size_t l = 0; l < rig.Layers.size(); ++l)
        if (const AnimBoundSelector* selector = SelectorOf(rig, l))
            for (std::size_t r = 0; r < selector->Rules.size(); ++r)
            {
                if (selector->Rules[r].Behavior != tag || (l == navigation.Layer && static_cast<int>(r) == navigation.Rule))
                    continue;
                PropertyNode other = NoteProperty(std::format("{}{}.{}", kSelectedBy, l, r), rig.Layers[l].NameText,
                                                  selector->Rules[r].Label);
                other.Descriptor.Verbs = { { .Id = "pick", .Label = "Inspect" } };
                others.Children.push_back(std::move(other));
            }
    if (!others.Children.empty())
        nodes.push_back(std::move(others));
    return nodes;
}

PropertyEditResult AnimationRuleProperties::CommitEdit(std::string_view key, std::string_view value)
{
    const AnimBoundRig* rig = Workspace.Rig.Simulation.Rig();
    if (rig == nullptr)
        return PropertyEditResult::Gone;
    if (key == "next")
    {
        const std::optional<bool> on = ParseToggle(value);
        if (!on)
            return PropertyEditResult::Refused;
        ShowNext = *on;
        return PropertyEditResult::Applied;
    }
    if (key != "layer")
        return PropertyEditResult::Gone;
    const std::optional<std::size_t> layer = ParseAnimationLayerChoice(*rig, value);
    if (!layer)
        return PropertyEditResult::Refused;
    Workspace.Rig.Navigation.Layer = *layer;
    Workspace.Rig.Navigation.Rule = -1;
    return PropertyEditResult::Applied;
}

PropertyEditResult AnimationRuleProperties::Act(std::string_view key, std::string_view verb, std::string_view)
{
    const AnimBoundRig* rig = Workspace.Rig.Simulation.Rig();
    if (rig == nullptr)
        return PropertyEditResult::Gone;
    AnimationNavigation& navigation = Workspace.Rig.Navigation;
    if (const std::optional<PropertyKeyIndex> rule = ParsePropertyKeyIndex(key, kRules);
        rule && rule->Rest.empty() && verb == "pick")
    {
        const AnimBoundSelector* selector = SelectorOf(*rig, navigation.Layer);
        if (selector == nullptr || rule->Index >= selector->Rules.size())
            return PropertyEditResult::Gone;
        navigation.Rule = static_cast<int>(rule->Index);
        navigation.Behavior = selector->Rules[rule->Index].Behavior;
        return PropertyEditResult::Applied;
    }
    if (const std::optional<PropertyKeyIndex> other = ParsePropertyKeyIndex(key, kSelectedBy); other && verb == "pick")
    {
        const std::optional<PropertyKeyIndex> ruled = ParsePropertyKeyIndex(other->Rest, "");
        if (!ruled || !ruled->Rest.empty())
            return PropertyEditResult::Gone;
        const std::size_t layer = other->Index;
        const std::size_t rule = ruled->Index;
        const AnimBoundSelector* selector = SelectorOf(*rig, layer);
        if (selector == nullptr || rule >= selector->Rules.size())
            return PropertyEditResult::Gone;
        navigation.Layer = layer;
        navigation.Rule = static_cast<int>(rule);
        navigation.Behavior = selector->Rules[rule].Behavior;
        return PropertyEditResult::Applied;
    }
    if (const std::optional<PropertyKeyIndex> slot = ParsePropertyKeyIndex(key, kSlots);
        slot && slot->Rest.empty() && slot->Index < rig->SlotRows.size())
    {
        const AnimBoundSlotRow& row = rig->SlotRows[slot->Index];
        if (verb == "audition")
            return Workspace.AuditionClip(ContentPath(*rig, row.Content)) ? PropertyEditResult::Applied
                                                                          : PropertyEditResult::Refused;
        if (verb == "open")
        {
            Open(row.DeclaredIn);
            return PropertyEditResult::Applied;
        }
    }
    if (verb == "open" && key == "source")
        if (const AnimBoundRule* rule = PickedRule(*rig, navigation); rule != nullptr && !rule->Source.empty())
        {
            Open(rule->Source.back().Selector);
            return PropertyEditResult::Applied;
        }
    if (verb == "open" && key == "behavior.declared")
        if (const AnimBoundRule* rule = PickedRule(*rig, navigation))
            if (const AnimBoundBehavior* behavior = rig->FindBehavior(rule->Behavior))
            {
                Open(behavior->DeclaredIn);
                return PropertyEditResult::Applied;
            }
    return PropertyEditResult::Gone;
}

std::vector<PropertyNode> AnimationRuleProperties::TickSource::Nodes() const
{
    const AnimationPreviewSession& session = Owner.Workspace.Rig.Simulation;
    const AnimBoundRig* rig = session.Rig();
    const AnimationPreviewTickRecord* record = Owner.Workspace.Rig.Inspected();
    if (rig == nullptr || record == nullptr)
        return { NoteProperty("tick.none", "Decisions", "Run the rig to record its decisions.") };

    PropertyNode tick = GroupProperty("tick", std::format("Tick {}{}", record->Tick,
                                                          record->FactsExact ? "" : ", derived facts warming up"));
    constexpr std::string_view kLatches[] = { "", ", latched", ", latch finishing", ", latch cancelling" };
    for (std::size_t l = 0; l < record->Layers.size() && l < rig->Layers.size(); ++l)
    {
        const AnimationPreviewLayerRecord& layer = record->Layers[l];
        const AnimBoundSelector* selector = SelectorOf(*rig, l);
        const std::string winner = selector == nullptr          ? std::string("request-keyed")
                                   : layer.Winner != kAnimNoRule ? selector->Rules[layer.Winner].Label
                                                                 : std::string("(none)");
        tick.Children.push_back(NoteProperty(
            std::format("tick.layer.{}", l), rig->Layers[l].NameText,
            std::format("{} \xE2\x86\x92 {}{}, {} at {:.2f}s{}", winner, session.TagName(layer.Behavior),
                        kLatches[static_cast<int>(layer.Latch)], ContentPath(*rig, layer.Content), layer.TimeSeconds,
                        layer.ContentComplete ? ", complete" : "")));
    }
    for (std::size_t d = 0; const AnimDecisionRecord& decision : record->Decisions)
    {
        std::string text = std::format("{} ({})", AnimDecisionCauseName(decision.Cause),
                                       AnimChangeReasonName(decision.Reason));
        if (decision.Behavior.IsValid())
            text += " " + session.TagName(decision.Behavior);
        const std::string where = decision.Layer != kAnimNoLayer && decision.Layer < rig->Layers.size()
                                      ? rig->Layers[decision.Layer].NameText
                                      : std::string("Rig");
        tick.Children.push_back(NoteProperty(std::format("tick.decision.{}", d++), where, text));
    }
    std::vector<PropertyNode> nodes{ std::move(tick) };

    // The entity's own log, newest first: what a game's decision trace keeps.
    if (const AnimDecisionLog* log = session.DecisionLog(); log != nullptr && log->Size() > 0)
    {
        PropertyNode history = GroupProperty("log", "Decision log");
        for (std::size_t i = log->Size(); i-- > 0 && history.Children.size() < kLogShown;)
        {
            const AnimDecisionRecord& logged = log->At(i);
            std::string text = std::format("{} #{}", AnimDecisionCauseName(logged.Cause), logged.Request.Sequence);
            if (logged.Intent.IsValid())
                text += " " + session.TagName(logged.Intent);
            if (logged.CancelReason != AnimCancelReason::None)
                text += std::format(" ({})", AnimCancelReasonName(logged.CancelReason));
            if (logged.RejectReason != AnimRejectReason::None)
                text += std::format(" ({})", AnimRejectReasonName(logged.RejectReason));
            history.Children.push_back(NoteProperty(std::format("log.{}", history.Children.size()),
                                                    std::format("tick {}", logged.Tick), text));
        }
        nodes.push_back(std::move(history));
    }
    return nodes;
}
