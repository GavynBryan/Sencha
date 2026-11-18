#include "AbilityPass.h"

#include <effects/EffectCost.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace
{
    bool Has(std::uint8_t groups, std::size_t group)
    {
        return (groups >> group) & 1u;
    }

    const AbilityCostProviderDefinition* SpenderOf(const AbilityCostProviderCatalog& catalog,
                                                   const AbilityCostProviderDefinition& named)
    {
        if (named.Translate == nullptr)
            return &named;
        const AbilityCostProviderDefinition* target = catalog.Get(catalog.Find(named.SpendsThrough));
        return target != nullptr && target->Translate == nullptr ? target : nullptr;
    }
}

// Every check before anything is applied, so a commit either pays all of it or none.
AbilityCommitResult AbilityPass::CheckCosts(const AbilityActor& actor, std::size_t slot,
                                            const AbilityDefinition& definition, std::uint8_t groups, bool cooldown,
                                            EntityId source)
{
    EffectCosts.clear();
    for (std::size_t g = 0; g < definition.Costs.size(); ++g)
    {
        if (!Has(groups, g))
            continue;
        const AbilityCostGroup& group = definition.Costs[g];
        const EntityId payer = group.Target == AbilityCostTarget::Source ? source : actor.Entity;
        for (const EffectId effect : group.Effects)
            EffectCosts.push_back({ payer, effect });
    }
    const EffectCostCheck check = CheckEffectCosts(Entities, EffectCosts);
    if (!check.Affordable())
    {
        AbilityCommitResult failed;
        for (std::size_t g = 0; g < definition.Costs.size(); ++g)
            if (Has(groups, g) && std::ranges::find(definition.Costs[g].Effects, check.Effect) != definition.Costs[g].Effects.end())
                failed.Group = static_cast<std::uint8_t>(g);
        return failed;
    }
    if (const std::optional<std::uint8_t> refused = CheckResources(actor, slot, definition, groups, source))
        return { .Paid = false, .Group = *refused };
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

std::optional<std::uint8_t> AbilityPass::CheckResources(const AbilityActor& actor, std::size_t slot,
                                                        const AbilityDefinition& definition, std::uint8_t groups,
                                                        EntityId source)
{
    CostArguments.clear();
    CostRows.clear();
    CostRowGroups.clear();
    CostRowArguments.clear();
    CostBatches.clear();
    const AbilityCostProviderCatalog* catalog = std::as_const(Entities).TryGetResource<AbilityCostProviderCatalog>();
    const auto named = [&](const AbilityCostResource& resource) -> const AbilityCostProviderDefinition* {
        return catalog != nullptr && catalog->IsCurrent(resource.Provider) ? catalog->Get(resource.Provider.Slot)
                                                                           : nullptr;
    };
    for (std::size_t g = 0; g < definition.Costs.size(); ++g)
    {
        if (!Has(groups, g))
            continue;
        for (const AbilityCostResource& resource : definition.Costs[g].Resources)
        {
            const AbilityCostProviderDefinition* provider = named(resource);
            const AbilityCostProviderDefinition* spender = provider != nullptr ? SpenderOf(*catalog, *provider) : nullptr;
            if (spender == nullptr)
                return static_cast<std::uint8_t>(g);
            if (std::ranges::find(CostBatches, spender, &AbilityCostBatch::Provider) == CostBatches.end())
                CostBatches.push_back({ .Provider = spender });
        }
    }
    // Each batch's rows are contiguous, so a provider is handed a span.
    for (AbilityCostBatch& batch : CostBatches)
    {
        batch.Begin = static_cast<std::uint32_t>(CostRows.size());
        for (std::size_t g = 0; g < definition.Costs.size(); ++g)
        {
            if (!Has(groups, g))
                continue;
            for (const AbilityCostResource& resource : definition.Costs[g].Resources)
            {
                const AbilityCostProviderDefinition* provider = named(resource);
                if (SpenderOf(*catalog, *provider) != batch.Provider)
                    continue;
                CostRowArguments.push_back(static_cast<std::uint32_t>(CostArguments.size()));
                if (provider->Translate == nullptr)
                    for (const AbilityValue& argument : resource.Arguments)
                        Evaluate(actor, slot, definition, argument, CostArguments.emplace_back());
                else
                {
                    CostTranslating.clear();
                    for (const AbilityValue& argument : resource.Arguments)
                        Evaluate(actor, slot, definition, argument, CostTranslating.emplace_back());
                    const AbilityCostRow own{ .Actor = actor.Entity, .Source = source, .Arguments = CostTranslating };
                    if (!provider->Translate(Entities, own, CostArguments))
                        return static_cast<std::uint8_t>(g);
                }
                CostRows.push_back({ .Actor = actor.Entity, .Source = source, .Arguments = {} });
                CostRowGroups.push_back(static_cast<std::uint8_t>(g));
            }
        }
        batch.End = static_cast<std::uint32_t>(CostRows.size());
    }
    CostRowArguments.push_back(static_cast<std::uint32_t>(CostArguments.size()));
    for (std::size_t r = 0; r < CostRows.size(); ++r)
        CostRows[r].Arguments = std::span<const AuthoredValue>(CostArguments).subspan(
            CostRowArguments[r], CostRowArguments[r + 1] - CostRowArguments[r]);

    for (const AbilityCostBatch& batch : CostBatches)
    {
        const std::span<const AbilityCostRow> rows(CostRows.data() + batch.Begin, batch.End - batch.Begin);
        const AbilityCostProviderCheck check = batch.Provider->Check(Entities, rows);
        if (!check.Affordable)
            return CostRowGroups[batch.Begin + std::min<std::size_t>(check.Row, rows.size() - 1)];
    }
    return std::nullopt;
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

    const AbilityGrant* grant = actor.Grant(record.Grant);
    const EntityId source = grant != nullptr && Entities.IsAlive(grant->Source) ? grant->Source : EntityId{};
    const AbilityCommitResult check = CheckCosts(actor, slot, definition, payable, cooldown, source);
    if (!check.Paid)
        return check;

    // A guess only chooses its path: what it would pay is the authority's to pay.
    // Effects first, then each provider in the order the definition names it.
    const FixedSimTime now = Now;
    if (!Predicting)
    {
        for (const EffectCostRow& cost : EffectCosts)
            (void)ApplyEffect(Entities, cost.Target, cost.Effect, now);
        for (const AbilityCostBatch& batch : CostBatches)
            batch.Provider->Apply(Entities, std::span<const AbilityCostRow>(CostRows.data() + batch.Begin,
                                                                            batch.End - batch.Begin));
    }
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
