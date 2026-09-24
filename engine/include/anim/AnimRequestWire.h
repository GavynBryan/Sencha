#pragma once

#include <anim/AnimRequestSet.h>
#include <core/metadata/ScalarGroup.h>
#include <core/metadata/TypeSchema.h>
#include <net/ReplicationLayout.h>

#include <cstdint>

//=============================================================================
// AnimRequestSet on the wire
//
// A request names an entity and gameplay tags, and both are this process's own
// numbering, so the set travels as an image in the names every machine shares:
// a NetEntityId for the source, a tag's wire key for the intent, the source
// tag and any param the record marks as a tag. Ticks travel as they are,
// because they are already the authority's, and so does the rig timing
// identity, which is built from names.
//
// A record is two runs -- its ticks and its words -- so a change to one record
// resends that record and the set's sequence and nothing else. A slot that is
// empty is all zeros. A source this machine was never sent arrives as no
// entity, and an intent this build does not register as no tag: the record
// stays, still ordered and still retained, and matches no rule.
//=============================================================================

// Source, StartTick, CancelTick, TailUntilTick, AnchorSectionStartTick.
struct AnimRequestWireTicks
{
    std::uint64_t Values[5] = {};
};

// Sequence, Intent, SourceTag, FixedTicks, Params[4], then the small fields
// packed as Layers | Lifetime << 8 | CancelReason << 16 | AnchorSection << 24,
// then Occupied | TagParams << 8.
struct AnimRequestWireWords
{
    std::uint32_t Values[10] = {};
};

struct AnimRequestWireRecord
{
    AnimRequestWireTicks Ticks;
    AnimRequestWireWords Words;
};

static_assert(kAnimRequestCapacity == 8, "AnimRequestSetWire names one member per record.");

struct AnimRequestSetWire
{
    AnimRequestWireRecord R0, R1, R2, R3, R4, R5, R6, R7;
    std::uint64_t RigTiming = 0;
    std::uint32_t NextSequence = 1;
};

template <>
struct PackedScalarGroup<AnimRequestWireTicks>
{
    static constexpr std::size_t Count = 5;
    using Component = std::uint64_t;
};

template <>
struct PackedScalarGroup<AnimRequestWireWords>
{
    static constexpr std::size_t Count = 10;
    using Component = std::uint32_t;
};

template <>
struct TypeSchema<AnimRequestWireRecord>
{
    static constexpr std::string_view Name = "anim_request_wire_record";
    static auto Fields()
    {
        return std::tuple{ MakeField("ticks", &AnimRequestWireRecord::Ticks),
                           MakeField("words", &AnimRequestWireRecord::Words) };
    }
};

template <>
struct TypeSchema<AnimRequestSetWire>
{
    static constexpr std::string_view Name = "anim_request_set_wire";
    static auto Fields()
    {
        return std::tuple{ MakeField("r0", &AnimRequestSetWire::R0), MakeField("r1", &AnimRequestSetWire::R1),
                           MakeField("r2", &AnimRequestSetWire::R2), MakeField("r3", &AnimRequestSetWire::R3),
                           MakeField("r4", &AnimRequestSetWire::R4), MakeField("r5", &AnimRequestSetWire::R5),
                           MakeField("r6", &AnimRequestSetWire::R6), MakeField("r7", &AnimRequestSetWire::R7),
                           MakeField("rig_timing", &AnimRequestSetWire::RigTiming),
                           MakeField("next_sequence", &AnimRequestSetWire::NextSequence) };
    }
};

template <>
struct ReplicationCodec<AnimRequestSet>
{
    using Wire = AnimRequestSetWire;
    static void ToWire(const ReplicationWireContext& context, const AnimRequestSet& set, AnimRequestSetWire& wire);
    static void FromWire(const ReplicationWireContext& context, const AnimRequestSetWire& wire, AnimRequestSet& set);
};
