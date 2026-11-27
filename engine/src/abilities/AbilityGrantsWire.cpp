#include <abilities/AbilityGrantsWire.h>

#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityRegistry.h>
#include <ecs/World.h>

void ReplicationCodec<AbilityGrants>::ToWire(const ReplicationWireContext& context, const AbilityGrants& grants,
                                             ReplicationImage& wire)
{
    wire.HeadAs<AbilityGrantsWireHead>().NextId = grants.NextId;
    if (context.Entities == nullptr)
        return;
    const auto* abilities = context.Entities->TryGetResource<AbilityRegistry>();
    const auto* entries = context.Entities->TryGetResource<AbilityGrantEntries>();
    const std::span<const AbilityGrant> held = AbilityGrantsOf(*context.Entities, grants);
    const std::span<AbilityGrantWire> sent = wire.Resize<AbilityGrantWire>(0, held.size());
    for (std::size_t i = 0; i < sent.size(); ++i)
    {
        const AbilityGrant& grant = held[i];
        std::uint32_t* words = sent[i].Words.Values;
        words[0] = grant.Id.Value;
        words[1] = abilities != nullptr ? abilities->WireKey(grant.Ability) : 0u;
        words[2] = entries != nullptr ? entries->WireKey(grant.Entry) : 0u;
        words[3] = static_cast<std::uint32_t>(grant.Kind);
        sent[i].Source = context.WireEntity(ResolvePersistableRef(*context.Entities, grant.Source));
    }
}

void ReplicationCodec<AbilityGrants>::FromWire(const ReplicationWireContext& context, const ReplicationImage& wire,
                                               AbilityGrants& grants)
{
    if (context.Receiving == nullptr)
        return;
    const auto* abilities = context.Receiving->TryGetResource<AbilityRegistry>();
    const auto* entries = context.Receiving->TryGetResource<AbilityGrantEntries>();
    AbilityGrantList arrived;
    arrived.NextId = wire.HeadAs<AbilityGrantsWireHead>().NextId;
    for (const AbilityGrantWire& sent : wire.List<AbilityGrantWire>(0))
    {
        const std::uint32_t* words = sent.Words.Values;
        if (words[0] == 0)
            continue;
        AbilityGrant grant;
        grant.Id = AbilityGrantId{ words[0] };
        grant.Ability = abilities != nullptr ? abilities->FindByWireKey(words[1]) : AbilityId{};
        grant.Entry = entries != nullptr && grant.Ability.IsValid() ? entries->FindByWireKey(words[2], grant.Ability)
                                                                    : kAbilityNoGrantEntry;
        grant.Kind = static_cast<AbilityGrantKind>(static_cast<std::uint8_t>(words[3]));
        grant.Source = PersistableRef{ .Entity = context.LocalEntity(sent.Source), .Target = {} };
        arrived.Entries.push_back(grant);
    }
    grants = MakeAbilityGrants(*context.Receiving, arrived);
}
