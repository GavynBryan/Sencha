#pragma once

#include <abilities/AbilityWirePrecision.h>
#include <abilities/AbilityCueLog.h>
#include <core/metadata/ScalarGroup.h>
#include <core/metadata/TypeSchema.h>
#include <math/MathSchemas.h>
#include <net/ReplicationLayout.h>

#include <cstdint>

// Activation, Tick, Target as a wire entity.
struct AbilityCueWireTicks
{
    std::uint64_t Values[3] = {};
};

// Seq | Op << 16; the cue's wire key; the ordinal.
struct AbilityCueWireWords
{
    std::uint32_t Values[3] = {};
};

struct AbilityCueWireEntry
{
    AbilityCueWireTicks Ticks;
    AbilityCueWireWords Words;
    Vec3d Point = Vec3d::Zero();
    Vec3d Normal = Vec3d::Zero();
};

// NextSeq, Overwritten.
struct AbilityCueWireCounters
{
    std::uint32_t Values[2] = {};
};

static_assert(kAbilityCueLogCapacity == 8, "AbilityCueLogWire names one member per entry.");

struct AbilityCueLogWire
{
    AbilityCueWireEntry E0, E1, E2, E3, E4, E5, E6, E7;
    AbilityCueWireCounters Counters;
};

template <>
struct PackedScalarGroup<AbilityCueWireTicks>
{
    static constexpr std::size_t Count = 3;
    using Component = std::uint64_t;
};

template <>
struct PackedScalarGroup<AbilityCueWireWords>
{
    static constexpr std::size_t Count = 3;
    using Component = std::uint32_t;
};

template <>
struct PackedScalarGroup<AbilityCueWireCounters>
{
    static constexpr std::size_t Count = 2;
    using Component = std::uint32_t;
};

template <>
struct TypeSchema<AbilityCueWireEntry>
{
    static constexpr std::string_view Name = "ability_cue_wire_entry";
    static auto Fields()
    {
        return std::tuple{
            MakeField("ticks", &AbilityCueWireEntry::Ticks),
            MakeField("words", &AbilityCueWireEntry::Words),
            MakeField("point", &AbilityCueWireEntry::Point)
                .Quantize(-kNetPositionRange, kNetPositionRange, kAbilityHitPointBits),
            MakeField("normal", &AbilityCueWireEntry::Normal)
                .Quantize(-1.0f, 1.0f, kAbilityHitNormalBits),
        };
    }
};

template <>
struct TypeSchema<AbilityCueLogWire>
{
    static constexpr std::string_view Name = "ability_cue_log_wire";
    static auto Fields()
    {
        return std::tuple{ MakeField("e0", &AbilityCueLogWire::E0), MakeField("e1", &AbilityCueLogWire::E1),
                           MakeField("e2", &AbilityCueLogWire::E2), MakeField("e3", &AbilityCueLogWire::E3),
                           MakeField("e4", &AbilityCueLogWire::E4), MakeField("e5", &AbilityCueLogWire::E5),
                           MakeField("e6", &AbilityCueLogWire::E6), MakeField("e7", &AbilityCueLogWire::E7),
                           MakeField("counters", &AbilityCueLogWire::Counters) };
    }
};

template <>
struct ReplicationCodec<AbilityCueLog>
{
    using Wire = AbilityCueLogWire;
    static void ToWire(const ReplicationWireContext& context, const AbilityCueLog& log, AbilityCueLogWire& wire);
    static void FromWire(const ReplicationWireContext& context, const AbilityCueLogWire& wire, AbilityCueLog& log);
};
