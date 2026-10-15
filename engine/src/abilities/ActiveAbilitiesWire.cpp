#include <abilities/ActiveAbilitiesWire.h>

#include <abilities/AbilityRegistry.h>
#include <ecs/World.h>

#include <array>

namespace
{
std::array<ActiveAbilityWireRecord*, kAbilityRecordCapacity> RecordsOf(ActiveAbilitiesWire& wire)
{
    return { &wire.R0, &wire.R1, &wire.R2, &wire.R3, &wire.R4, &wire.R5 };
}

std::array<const ActiveAbilityWireRecord*, kAbilityRecordCapacity> RecordsOf(const ActiveAbilitiesWire& wire)
{
    return { &wire.R0, &wire.R1, &wire.R2, &wire.R3, &wire.R4, &wire.R5 };
}

const AbilityRegistry* AbilitiesOf(const ReplicationWireContext& context)
{
    return context.Entities != nullptr ? context.Entities->TryGetResource<AbilityRegistry>() : nullptr;
}
}

void ReplicationCodec<ActiveAbilities>::ToWire(const ReplicationWireContext& context, const ActiveAbilities& active,
                                               ActiveAbilitiesWire& wire)
{
    const AbilityRegistry* abilities = AbilitiesOf(context);
    const auto records = RecordsOf(wire);
    for (std::size_t i = 0; i < kAbilityRecordCapacity; ++i)
    {
        ActiveAbilityWireRecord& out = *records[i];
        out = ActiveAbilityWireRecord{};
        const AbilityRecord& record = active.Records[i];
        if (record.State == AbilityRecordState::Free)
            continue;
        out.Ticks.Values[0] = record.Key.CommandTick;
        out.Words.Values[0] = abilities != nullptr ? abilities->WireKey(record.Key.Ability) : 0u;
        out.Words.Values[1] = static_cast<std::uint32_t>(record.Key.Origin)
                            | static_cast<std::uint32_t>(record.State) << 8
                            | static_cast<std::uint32_t>(record.Phase) << 16
                            | static_cast<std::uint32_t>(record.EndReason) << 24;
        out.Words.Values[2] = record.EndElement;
        out.Words.Values[3] = record.Grant.Value;
        out.Ticks.Values[1] = record.PhaseStart;
        out.Ticks.Values[2] = record.EndTick;
        out.Pulse = record.Pulse;
        out.LastPoint = record.LastPoint;
        out.LastNormal = record.LastNormal;
        out.LastTarget = context.WireEntity(record.LastTarget);
    }
}

void ReplicationCodec<ActiveAbilities>::FromWire(const ReplicationWireContext& context, const ActiveAbilitiesWire& wire,
                                                 ActiveAbilities& active)
{
    const AbilityRegistry* abilities = AbilitiesOf(context);
    const auto records = RecordsOf(wire);
    for (std::size_t i = 0; i < kAbilityRecordCapacity; ++i)
    {
        const ActiveAbilityWireRecord& in = *records[i];
        AbilityRecord& record = active.Records[i];
        record = AbilityRecord{};
        const auto state = static_cast<AbilityRecordState>(static_cast<std::uint8_t>(in.Words.Values[1] >> 8));
        if (state == AbilityRecordState::Free)
            continue;
        record.State = state;
        record.Key.CommandTick = in.Ticks.Values[0];
        record.Key.Ability = abilities != nullptr ? abilities->FindByWireKey(in.Words.Values[0]) : AbilityId{};
        record.Key.Origin = static_cast<std::uint8_t>(in.Words.Values[1]);
        record.Phase = static_cast<std::uint8_t>(in.Words.Values[1] >> 16);
        record.EndReason = static_cast<AbilityEndReason>(static_cast<std::uint8_t>(in.Words.Values[1] >> 24));
        record.EndElement = static_cast<std::uint16_t>(in.Words.Values[2]);
        record.Grant = AbilityGrantId{ in.Words.Values[3] };
        record.PhaseStart = in.Ticks.Values[1];
        record.EndTick = in.Ticks.Values[2];
        record.Identity = AbilityActivationIdentity(record.Key, in.Words.Values[0], record.Grant);
        record.Pulse = in.Pulse;
        record.LastPoint = in.LastPoint;
        record.LastNormal = in.LastNormal;
        record.LastTarget = context.LocalEntity(in.LastTarget);
    }
}
