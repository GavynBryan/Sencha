#include <abilities/AbilityGrantsWire.h>

#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityRegistry.h>
#include <ecs/World.h>

namespace
{
    constexpr AbilityGrantsWirePage AbilityGrantsWire::* kWirePages[kAbilityGrantWirePages] = {
        &AbilityGrantsWire::P0,  &AbilityGrantsWire::P1,  &AbilityGrantsWire::P2,  &AbilityGrantsWire::P3,
        &AbilityGrantsWire::P4,  &AbilityGrantsWire::P5,  &AbilityGrantsWire::P6,  &AbilityGrantsWire::P7,
        &AbilityGrantsWire::P8,  &AbilityGrantsWire::P9,  &AbilityGrantsWire::P10, &AbilityGrantsWire::P11,
        &AbilityGrantsWire::P12, &AbilityGrantsWire::P13, &AbilityGrantsWire::P14, &AbilityGrantsWire::P15,
    };
}

void ReplicationCodec<AbilityGrants>::ToWire(const ReplicationWireContext& context, const AbilityGrants& grants,
                                             AbilityGrantsWire& wire)
{
    wire = AbilityGrantsWire{};
    wire.NextId = grants.NextId;
    if (context.Entities == nullptr)
        return;
    const auto* abilities = context.Entities->TryGetResource<AbilityRegistry>();
    const auto* entries = context.Entities->TryGetResource<AbilityGrantEntries>();
    const std::span<const AbilityGrant> held = AbilityGrantsOf(*context.Entities, grants);
    for (std::size_t i = 0; i < held.size() && i < kAbilityGrantCapacity; ++i)
    {
        const AbilityGrant& grant = held[i];
        AbilityGrantsWirePage& page = wire.*kWirePages[i / kAbilityGrantsPerWirePage];
        const std::size_t at = i % kAbilityGrantsPerWirePage;
        std::uint32_t* words = &page.Words.Values[4 * at];
        words[0] = grant.Id.Value;
        words[1] = abilities != nullptr ? abilities->WireKey(grant.Ability) : 0u;
        words[2] = entries != nullptr ? entries->WireKey(grant.Entry) : 0u;
        words[3] = static_cast<std::uint32_t>(grant.Kind);
        page.Sources.Values[at] = context.WireEntity(grant.Source);
    }
}

void ReplicationCodec<AbilityGrants>::FromWire(const ReplicationWireContext& context, const AbilityGrantsWire& wire,
                                               AbilityGrants& grants)
{
    if (context.Receiving == nullptr)
        return;
    const auto* abilities = context.Receiving->TryGetResource<AbilityRegistry>();
    const auto* entries = context.Receiving->TryGetResource<AbilityGrantEntries>();
    AbilityGrantList arrived;
    arrived.NextId = wire.NextId;
    for (std::size_t i = 0; i < kAbilityGrantCapacity; ++i)
    {
        const AbilityGrantsWirePage& page = wire.*kWirePages[i / kAbilityGrantsPerWirePage];
        const std::size_t at = i % kAbilityGrantsPerWirePage;
        const std::uint32_t* words = &page.Words.Values[4 * at];
        if (words[0] == 0)
            continue;
        AbilityGrant grant;
        grant.Id = AbilityGrantId{ words[0] };
        grant.Ability = abilities != nullptr ? abilities->FindByWireKey(words[1]) : AbilityId{};
        grant.Entry = entries != nullptr && grant.Ability.IsValid() ? entries->FindByWireKey(words[2], grant.Ability)
                                                                    : kAbilityNoGrantEntry;
        grant.Kind = static_cast<AbilityGrantKind>(static_cast<std::uint8_t>(words[3]));
        grant.Source = context.LocalEntity(page.Sources.Values[at]);
        arrived.Entries.push_back(grant);
    }
    grants = MakeAbilityGrants(*context.Receiving, arrived);
}
