#include <abilities/AbilityGrantsWire.h>

#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityRegistry.h>
#include <core/hash/Fnv1a.h>
#include <ecs/World.h>

void ReplicationCodec<AbilityGrants>::ToWire(const ReplicationWireContext& context, const AbilityGrants& grants,
                                             AbilityGrantsWire& wire)
{
    wire = AbilityGrantsWire{};
    wire.NextId = grants.NextId;
    if (context.Entities == nullptr)
        return;
    const auto* abilities = context.Entities->TryGetResource<AbilityRegistry>();
    const auto* entries = context.Entities->TryGetResource<AbilityGrantEntries>();
    for (std::uint8_t i = 0; i < grants.Count; ++i)
    {
        const AbilityGrant& grant = grants.Entries[i];
        const AbilityGrantEntry* entry = entries != nullptr ? entries->Find(grant.Entry) : nullptr;
        std::uint32_t* words = &wire.Words.Values[4 * i];
        words[0] = grant.Id.Value;
        words[1] = abilities != nullptr ? abilities->WireKey(grant.Ability) : 0u;
        words[2] = entry != nullptr ? NameWireKey(entry->Key) : 0u;
        words[3] = static_cast<std::uint32_t>(grant.Kind);
        wire.Effects.Values[i] = context.WireEntity(grant.Effect);
    }
}

void ReplicationCodec<AbilityGrants>::FromWire(const ReplicationWireContext& context, const AbilityGrantsWire& wire,
                                               AbilityGrants& grants)
{
    grants = AbilityGrants{};
    grants.NextId = wire.NextId;
    if (context.Entities == nullptr)
        return;
    const auto* abilities = context.Entities->TryGetResource<AbilityRegistry>();
    const auto* entries = context.Entities->TryGetResource<AbilityGrantEntries>();
    for (std::size_t i = 0; i < kAbilityGrantCapacity; ++i)
    {
        const std::uint32_t* words = &wire.Words.Values[4 * i];
        if (words[0] == 0)
            continue;
        AbilityGrant& grant = grants.Entries[grants.Count++];
        grant.Id = AbilityGrantId{ words[0] };
        grant.Ability = abilities != nullptr ? abilities->FindByWireKey(words[1]) : AbilityId{};
        grant.Entry = entries != nullptr && grant.Ability.IsValid() ? entries->FindByWireKey(words[2], grant.Ability)
                                                                    : kAbilityNoGrantEntry;
        grant.Kind = static_cast<AbilityGrantKind>(static_cast<std::uint8_t>(words[3]));
        grant.Effect = context.LocalEntity(wire.Effects.Values[i]);
    }
}
