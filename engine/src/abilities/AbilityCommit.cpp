#include "AbilityPass.h"

#include <effects/EffectCost.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>

#include <algorithm>

namespace
{
    bool Has(std::uint8_t groups, std::size_t group)
    {
        return (groups >> group) & 1u;
    }
}

// Every check before anything is applied, so a commit either pays all of it or none.
AbilityCommitResult AbilityPass::CheckCosts(const AbilityActor& actor, const AbilityDefinition& definition,
                                            std::uint8_t groups, bool cooldown)
{
    Costs.clear();
    for (std::size_t g = 0; g < definition.Costs.size(); ++g)
        if (Has(groups, g))
            Costs.insert(Costs.end(), definition.Costs[g].Effects.begin(), definition.Costs[g].Effects.end());

    const EffectCostCheck check = CheckEffectCosts(Entities, actor.Entity, Costs);
    if (!check.Affordable())
    {
        AbilityCommitResult failed;
        for (std::size_t g = 0; g < definition.Costs.size(); ++g)
            if (Has(groups, g) && std::ranges::find(definition.Costs[g].Effects, check.Effect) != definition.Costs[g].Effects.end())
                failed.Group = static_cast<std::uint8_t>(g);
        return failed;
    }
    if (!cooldown || !definition.Cooldown)
        return { .Paid = true };

    // The cooldown cannot fail after the costs are paid: its definition and the
    // room for the tags it grants are checked here.
    const EffectRegistry* effects = static_cast<const World&>(Entities).TryGetResource<EffectRegistry>();
    const EffectDefinition* lasting = effects != nullptr ? effects->Get(definition.Cooldown->Effect) : nullptr;
    if (lasting == nullptr)
        return { .Paid = false, .Group = definition.Cooldown->Group };
    if (const GameplayTagContainer* tags = actor.Tags())
    {
        const auto fresh = std::ranges::count_if(lasting->GrantedTags, [&](GameplayTagId tag) {
            return tags->StackCount(tag) == 0;
        });
        if (tags->Size() + fresh > GameplayTagContainer::Capacity)
            return { .Paid = false, .Group = definition.Cooldown->Group };
    }
    return { .Paid = true };
}

AbilityCommitResult AbilityPass::Commit(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition,
                                        std::uint8_t groups)
{
    AbilityRecord& record = actor.Record(slot);
    std::uint8_t payable = 0;
    for (std::size_t g = 0; g < definition.Costs.size(); ++g)
    {
        if (!Has(groups, g))
            continue;
        if (definition.Costs[g].Repeat == AbilityCostRepeat::Once && Has(record.PaidGroups, g))
            Trace({ .Actor = actor.Entity, .Key = record.Key, .Event = AbilityTraceEvent::AlreadyCommitted,
                    .Element = static_cast<std::uint16_t>(g) });
        else
            payable |= static_cast<std::uint8_t>(1u << g);
    }
    const bool cooldown = definition.Cooldown && !record.CooldownStarted
        && definition.Cooldown->Start == AbilityCooldownStart::OnCommit && Has(groups, definition.Cooldown->Group);
    if (payable == 0 && !cooldown)
        return { .Paid = true };

    const AbilityCommitResult check = CheckCosts(actor, definition, payable, cooldown);
    if (!check.Paid)
        return check;

    // A guess only chooses its path: what it would pay is the authority's to pay.
    const FixedSimTime now = Now;
    if (!Predicting)
        for (const EffectId cost : Costs)
            (void)ApplyEffect(Entities, actor.Entity, cost, now);
    AbilityRecord& paid = actor.Record(slot);
    for (std::size_t g = 0; g < definition.Costs.size(); ++g)
        if (Has(payable, g) && definition.Costs[g].Repeat == AbilityCostRepeat::Once)
            paid.PaidGroups |= static_cast<std::uint8_t>(1u << g);
    if (cooldown)
        StartCooldown(actor, slot, definition);
    return { .Paid = true };
}

void AbilityPass::StartCooldown(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition)
{
    if (!Predicting)
        (void)ApplyEffect(Entities, actor.Entity, definition.Cooldown->Effect, Now);
    actor.Record(slot).CooldownStarted = true;
}
