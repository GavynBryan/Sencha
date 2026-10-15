#pragma once

#include <abilities/AbilityWirePrecision.h>
#include <abilities/ActiveAbilities.h>
#include <core/metadata/ScalarGroup.h>
#include <core/metadata/TypeSchema.h>
#include <math/MathSchemas.h>
#include <net/ReplicationLayout.h>

#include <cstdint>

// What a record says to every machine that presents it: its activation, phase,
// pulses, last hit and end. Leases, owned tags and the trigger stay with the
// authority; the grant goes only as the number the activation is named by. One run
// per rate of change, so a pulse resends only the count and the hit
// (docs/gameplay/abilitykit.md, "Replication").

// CommandTick, PhaseStart, EndTick: at its start, each phase and its end.
struct ActiveAbilityWireTicks
{
    std::uint64_t Values[3] = {};
};

// The ability's wire key; Origin | State << 8 | Phase << 16 | EndReason << 24;
// EndElement; the grant's id.
struct ActiveAbilityWireWords
{
    std::uint32_t Values[4] = {};
};

struct ActiveAbilityWireRecord
{
    ActiveAbilityWireTicks Ticks;
    ActiveAbilityWireWords Words;
    std::uint32_t Pulse = 0;
    Vec3d LastPoint = Vec3d::Zero();
    Vec3d LastNormal = Vec3d::Zero();
    std::uint64_t LastTarget = 0;
};

static_assert(kAbilityRecordCapacity == 6, "ActiveAbilitiesWire names one member per record.");

struct ActiveAbilitiesWire
{
    ActiveAbilityWireRecord R0, R1, R2, R3, R4, R5;
};


template <>
struct PackedScalarGroup<ActiveAbilityWireWords>
{
    static constexpr std::size_t Count = 4;
    using Component = std::uint32_t;
};

template <>
struct PackedScalarGroup<ActiveAbilityWireTicks>
{
    static constexpr std::size_t Count = 3;
    using Component = std::uint64_t;
};

template <>
struct TypeSchema<ActiveAbilityWireRecord>
{
    static constexpr std::string_view Name = "active_ability_wire_record";
    static auto Fields()
    {
        return std::tuple{
            MakeField("ticks", &ActiveAbilityWireRecord::Ticks),
            MakeField("words", &ActiveAbilityWireRecord::Words),
            MakeField("pulse", &ActiveAbilityWireRecord::Pulse),
            MakeField("last_point", &ActiveAbilityWireRecord::LastPoint)
                .Quantize(-kNetPositionRange, kNetPositionRange, kAbilityHitPointBits),
            MakeField("last_normal", &ActiveAbilityWireRecord::LastNormal)
                .Quantize(-1.0f, 1.0f, kAbilityHitNormalBits),
            MakeField("last_target", &ActiveAbilityWireRecord::LastTarget),
        };
    }
};

template <>
struct TypeSchema<ActiveAbilitiesWire>
{
    static constexpr std::string_view Name = "active_abilities_wire";
    static auto Fields()
    {
        return std::tuple{ MakeField("r0", &ActiveAbilitiesWire::R0), MakeField("r1", &ActiveAbilitiesWire::R1),
                           MakeField("r2", &ActiveAbilitiesWire::R2), MakeField("r3", &ActiveAbilitiesWire::R3),
                           MakeField("r4", &ActiveAbilitiesWire::R4), MakeField("r5", &ActiveAbilitiesWire::R5) };
    }
};

template <>
struct ReplicationCodec<ActiveAbilities>
{
    using Wire = ActiveAbilitiesWire;
    static void ToWire(const ReplicationWireContext& context, const ActiveAbilities& active, ActiveAbilitiesWire& wire);
    static void FromWire(const ReplicationWireContext& context, const ActiveAbilitiesWire& wire, ActiveAbilities& active);
};
