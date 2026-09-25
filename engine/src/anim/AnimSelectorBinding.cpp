#include "AnimRigBinder.h"

#include <anim/AnimSelectorData.h>

#include <algorithm>
#include <format>
#include <numeric>

namespace
{
    // What a nested selector's rules inherit from the rule that delegated to
    // them: its condition, its standing, and where it came from.
    struct ParentContext
    {
        AnimProgram Enter;
        AnimProgram Stay;
        bool HasStay = false;
        std::int32_t PriorityBand = 0;
        std::vector<AnimRuleSource> Source;
        std::vector<AnimRowSource> EnterRows;
        std::vector<AnimRowSource> StayRows;
        std::string KeyPrefix;
        bool RequiresRequest = false;
    };

    struct SelectorFlattener
    {
        AnimRigBinder& Binder;
        const AnimRigData& Rig;
        AnimBoundSelector& Out;
        std::vector<std::string> Visiting;

        AnimProgram Compile(const AnimPredicateDecl& decl, const std::string& asset, const std::string& path)
        {
            std::vector<AnimDiagnostic> problems;
            AnimProgram program = CompileAnimPredicate(decl, Binder.Out, Binder.Tags(), asset, path, problems);
            for (AnimDiagnostic& problem : problems)
                Binder.Report(problem.Severity, std::move(problem.Code), std::move(problem.AssetPath),
                              std::move(problem.FieldPath), std::move(problem.Message));
            return program;
        }

        void Flatten(const std::string& path, const std::string& referrer, const std::string& field,
                     const ParentContext& parent, std::size_t depth)
        {
            if (depth >= kAnimMaxSelectorDepth)
            {
                Binder.Error("anim.selector.depth", referrer, field,
                             std::format("Delegation nests deeper than {} selectors here.",
                                         kAnimMaxSelectorDepth));
                return;
            }
            if (std::find(Visiting.begin(), Visiting.end(), path) != Visiting.end())
            {
                Binder.Error("anim.selector.cycle", referrer, field,
                             std::format("'{}' delegates back into itself.", path));
                return;
            }
            const AnimSelectorData* selector =
                Binder.Load<AnimSelectorData>(path, kAnimSelectorType, referrer, field);
            if (selector == nullptr)
                return;
            Visiting.push_back(path);

            // Priority first; equal priorities keep their authored order.
            std::vector<std::size_t> order(selector->Rules.size());
            std::iota(order.begin(), order.end(), 0);
            std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                return selector->Rules[a].Priority > selector->Rules[b].Priority;
            });

            for (const std::size_t index : order)
            {
                const AnimSelectorRuleDecl& rule = selector->Rules[index];
                const std::string at = std::format("$.data.rules[{}]", index);

                ParentContext context;
                const AnimProgram enter = Compile(rule.Enter, path, at + ".enter");
                const AnimProgram stay = rule.HasStay ? Compile(rule.Stay, path, at + ".stay") : enter;
                context.Enter = ConcatAnimPrograms(parent.Enter, enter);
                // A context's stay is always complete -- its enter where no stay
                // was authored -- so a nested rule's is its parent's then its own.
                context.Stay = ConcatAnimPrograms(parent.Stay, stay);
                context.HasStay = parent.HasStay || rule.HasStay;
                context.RequiresRequest = parent.RequiresRequest || AnimPredicateRequiresRequest(rule.Enter);
                context.PriorityBand = depth == 0 ? rule.Priority : parent.PriorityBand;
                context.EnterRows = parent.EnterRows;
                context.StayRows = parent.StayRows;
                const auto rowIndex = static_cast<std::uint32_t>(index);
                for (std::uint32_t r = 0; r < rule.Enter.Rows.size(); ++r)
                    context.EnterRows.push_back({ path, rowIndex, false, r });
                const std::size_t stayRows = rule.HasStay ? rule.Stay.Rows.size() : rule.Enter.Rows.size();
                for (std::uint32_t r = 0; r < stayRows; ++r)
                    context.StayRows.push_back({ path, rowIndex, rule.HasStay, r });
                context.Source = parent.Source;
                context.Source.push_back({ path, static_cast<std::uint32_t>(index), rule.Name });
                // Named rules keep their key when reordered; unnamed ones are
                // known only by position.
                context.KeyPrefix = rule.Name.empty()
                    ? std::format("{}{}#{}/", parent.KeyPrefix, path, index)
                    : std::format("{}{}#{}/", parent.KeyPrefix, path, rule.Name);

                switch (rule.Result)
                {
                case AnimRuleResultKind::Behavior:
                    AddRule(rule, context, path, at);
                    break;
                case AnimRuleResultKind::Weight:
                    AddWeightRule(rule, context, path, at);
                    break;
                case AnimRuleResultKind::Delegate:
                    Flatten(rule.Delegate, path, at + ".delegate", context, depth + 1);
                    break;
                case AnimRuleResultKind::Extension:
                {
                    // An unbound extension point contributes nothing: that is
                    // what it is before a game or mod binds one.
                    const auto bound = std::find_if(Rig.Extensions.begin(), Rig.Extensions.end(),
                                                    [&](const AnimRigExtension& extension) {
                                                        return extension.Name == rule.Extension;
                                                    });
                    if (bound != Rig.Extensions.end())
                        Flatten(bound->SelectorPath, path, at + ".extension", context, depth + 1);
                    break;
                }
                }
            }
            Visiting.pop_back();
        }

        void AddRule(const AnimSelectorRuleDecl& rule, ParentContext& context, const std::string& path,
                     const std::string& at)
        {
            const std::optional<GameplayTagId> behavior =
                Binder.ResolveTag(rule.Behavior, path, at + ".behavior", "anim.selector.behavior_unresolved");
            if (!behavior)
                return;
            AnimBoundRule bound;
            bound.PriorityBand = context.PriorityBand;
            bound.Enter = std::move(context.Enter);
            bound.Stay = std::move(context.Stay);
            bound.HasStay = context.HasStay;
            bound.Behavior = *behavior;
            bound.BehaviorIndex = Binder.Out.FindBehaviorIndex(*behavior);
            bound.HoldMinMs = rule.HoldMinMs;
            bound.CooldownMs = rule.CooldownMs;
            bound.Source = std::move(context.Source);
            bound.EnterRows = std::move(context.EnterRows);
            bound.StayRows = std::move(context.StayRows);
            bound.Key = AnimStableKey(context.KeyPrefix);
            bound.Label = rule.Name.empty() ? rule.Behavior : rule.Name;
            if (bound.Enter.Intents.size() == 1)
                bound.LatchIntent = bound.Enter.Intents.front();
            bound.RequiresRequest = context.RequiresRequest;

            if (bound.BehaviorIndex < 0)
            {
                Binder.Error("anim.selector.undeclared_behavior", path, at + ".behavior",
                             std::format("'{}' is not declared by any of the rig's behavior sets.",
                                         rule.Behavior));
                return;
            }
            Validate(bound, path, at);
            Out.Rules.push_back(std::move(bound));
        }

        void AddWeightRule(const AnimSelectorRuleDecl& rule, ParentContext& context, const std::string& path,
                           const std::string& at)
        {
            AnimBoundWeightRule bound;
            bound.Enter = std::move(context.Enter);
            bound.Value = rule.Weight;
            bound.Source = std::move(context.Source);
            bound.EnterRows = std::move(context.EnterRows);
            bound.Key = AnimStableKey(context.KeyPrefix);
            bound.Label = rule.Name.empty() ? std::format("weight {}", rule.Weight) : rule.Name;
            if (!rule.WeightFact.empty())
            {
                bound.FactSlot = Binder.Out.FindSlot(rule.WeightFact);
                if (bound.FactSlot < 0
                    || Binder.Out.Slots[static_cast<std::size_t>(bound.FactSlot)].Kind != AnimFactKind::Float)
                {
                    Binder.Error("anim.selector.weight_fact", path, at + ".weight_fact",
                                 std::format("'{}' is not a float fact of this rig, so it cannot weight a layer.",
                                             rule.WeightFact));
                    return;
                }
                if (rule.Name.empty())
                    bound.Label = rule.WeightFact;
            }
            Out.WeightRules.push_back(std::move(bound));
        }

        // The pairings a behavior requires of each rule reaching it.
        void Validate(const AnimBoundRule& rule, const std::string& path, const std::string& at)
        {
            const AnimBoundBehavior& behavior = Binder.Out.Behaviors[static_cast<std::size_t>(rule.BehaviorIndex)];
            const AnimBehaviorDecl& policy = behavior.Policy;
            // Reached without a request: nothing replicates a start tick for
            // it, so a late joiner cannot rebuild it and it cannot move the
            // character.
            if (!rule.RequiresRequest)
            {
                if (policy.LateJoin == AnimLateJoin::Reconstruct)
                    Binder.Error("anim.selector.cosmetic_reconstruct", path, at + ".enter",
                                 std::format("'{}' is reached here without a request, so it has no start "
                                             "tick to reconstruct from; use Skip or SnapToEnd.",
                                             behavior.Name));
                if (policy.RootMotion)
                    Binder.Error("anim.selector.cosmetic_root_motion", path, at + ".enter",
                                 std::format("'{}' moves the character, and is reached here without a "
                                             "request that anchors it.",
                                             behavior.Name));
            }
            if (policy.Latch.Mode == AnimLatchMode::UntilRequestEnds && !rule.LatchIntent.IsValid())
                Binder.Error("anim.selector.latch_request", path, at + ".enter",
                             std::format("'{}' latches until its request ends, so the rule that selects it "
                                         "reads exactly one request; this one reads {}.",
                                         behavior.Name, rule.Enter.Intents.size()));
        }
    };
}

void AnimRigBinder::BindSelectors(const AnimRigData& rig)
{
    for (std::size_t l = 0; l < rig.Layers.size() && l < Out.Layers.size(); ++l)
    {
        const std::string& path = rig.Layers[l].SelectorPath;
        if (path.empty())
            continue;

        AnimBoundSelector selector;
        selector.Path = path;
        SelectorFlattener flattener{ *this, rig, selector, {} };
        flattener.Flatten(path, Out.RigPath, std::format("$.data.layers[{}].selector", l), ParentContext{}, 0);

        int cooldowns = 0;
        for (AnimBoundRule& rule : selector.Rules)
        {
            if (rule.CooldownMs > 0.0f)
                rule.CooldownSlot = cooldowns++;
            selector.ReadsTime = selector.ReadsTime || rule.Enter.ReadsTime || rule.Stay.ReadsTime;
            selector.ReadsTags = selector.ReadsTags || !rule.Enter.Queries.empty() || !rule.Stay.Queries.empty();
        }
        for (const AnimBoundWeightRule& rule : selector.WeightRules)
        {
            selector.ReadsTime = selector.ReadsTime || rule.Enter.ReadsTime;
            selector.ReadsTags = selector.ReadsTags || !rule.Enter.Queries.empty();
        }
        if (static_cast<std::size_t>(cooldowns) > kAnimCooldownSlots)
            Error("anim.selector.cooldowns", path, "$.data.rules",
                  std::format("{} rules on this layer have a cooldown, and each entity keeps at most {}.",
                              cooldowns, kAnimCooldownSlots));

        Out.Layers[l].Selector = static_cast<int>(Out.Selectors.size());
        Out.Selectors.push_back(std::move(selector));
    }
}
