#include <effects/EffectCost.h>

#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <effects/EffectDefinition.h>
#include <effects/EffectRegistry.h>

#include <ecs/World.h>

#include <algorithm>
#include <array>
#include <utility>

namespace
{
struct AttributeDelta
{
    AttributeId Attribute;
    float Delta = 0.0f;
};

bool IsInstantCost(const EffectDefinition& def)
{
    if (def.Duration != EffectDuration::Instant)
        return false;
    for (const EffectModifier& modifier : def.Modifiers)
        if (modifier.Op != ModifierOp::Add)
            return false;
    return true;
}
} // namespace

EffectCostCheck CheckEffectCosts(const World& world, EntityId actor, std::span<const EffectId> costs)
{
    if (costs.empty())
        return {};

    const EffectRegistry* effects = world.TryGetResource<EffectRegistry>();
    const AttributeRegistry* attributes = world.TryGetResource<AttributeRegistry>();
    const AttributeSet* set = world.TryGet<AttributeSet>(actor);

    // Every attribute a cost names is in the actor's set, so the set's capacity
    // bounds the distinct attributes.
    std::array<AttributeDelta, AttributeSet::Capacity> deltas{};
    std::size_t deltaCount = 0;

    for (EffectId cost : costs)
    {
        const EffectDefinition* def = effects != nullptr ? effects->Get(cost) : nullptr;
        if (def == nullptr)
            return { .Status = EffectCostStatus::DefinitionMissing, .Effect = cost };
        if (!IsInstantCost(*def))
            return { .Status = EffectCostStatus::NotAnInstantCost, .Effect = cost };
        if (set == nullptr)
            return { .Status = EffectCostStatus::AttributeSetMissing, .Effect = cost };

        for (const EffectModifier& modifier : def->Modifiers)
        {
            if (attributes == nullptr || !attributes->IsKnown(modifier.Attr) || !set->Has(modifier.Attr))
                return { .Status = EffectCostStatus::AttributeMissing, .Effect = cost, .Attribute = modifier.Attr };

            std::size_t slot = 0;
            while (slot < deltaCount && deltas[slot].Attribute != modifier.Attr)
                ++slot;
            if (slot == deltaCount)
                deltas[deltaCount++] = { modifier.Attr, 0.0f };
            deltas[slot].Delta += modifier.Magnitude;
        }
    }

    for (std::size_t i = 0; i < deltaCount; ++i)
    {
        const AttributeDelta& delta = deltas[i];
        if (delta.Delta >= 0.0f)
            continue;
        const float floor = std::max(0.0f, attributes->Min(delta.Attribute));
        const float available = set->GetBase(delta.Attribute) - floor;
        if (available < -delta.Delta)
            return { .Status = EffectCostStatus::Insufficient,
                     .Attribute = delta.Attribute,
                     .Needed = -delta.Delta,
                     .Available = available };
    }
    return {};
}
