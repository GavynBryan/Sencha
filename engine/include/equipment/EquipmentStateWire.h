#pragma once

#include <core/metadata/ScalarGroup.h>
#include <core/metadata/TypeSchema.h>
#include <equipment/EquipmentComponents.h>
#include <net/ReplicationLayout.h>

#include <cstdint>

// Per record: its key, its definition's wire key, its slots, its primary slot.
struct EquipmentWireWords
{
    std::uint32_t Values[4] = {};
};

// One run per record, so a change to one record resends that record alone; the
// source is its owner's to see.
struct EquipmentWireRecord
{
    EquipmentWireWords Words;
    std::uint64_t Source = 0;
};

static_assert(kEquipmentRecordCapacity == 16, "EquipmentStateWire names one member per record.");

struct EquipmentStateWire
{
    std::uint32_t Layout = 0;
    EquipmentWireRecord R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15;
};

template <>
struct PackedScalarGroup<EquipmentWireWords>
{
    static constexpr std::size_t Count = 4;
    using Component = std::uint32_t;
};

template <>
struct TypeSchema<EquipmentWireRecord>
{
    static constexpr std::string_view Name = "equipment_wire_record";
    static auto Fields()
    {
        return std::tuple{ MakeField("words", &EquipmentWireRecord::Words),
                           MakeField("source", &EquipmentWireRecord::Source).OwnerOnly() };
    }
};

template <>
struct TypeSchema<EquipmentStateWire>
{
    static constexpr std::string_view Name = "equipment_state_wire";
    static auto Fields()
    {
        using W = EquipmentStateWire;
        return std::tuple{ MakeField("layout", &W::Layout), MakeField("r0", &W::R0),   MakeField("r1", &W::R1),
                           MakeField("r2", &W::R2),         MakeField("r3", &W::R3),   MakeField("r4", &W::R4),
                           MakeField("r5", &W::R5),         MakeField("r6", &W::R6),   MakeField("r7", &W::R7),
                           MakeField("r8", &W::R8),         MakeField("r9", &W::R9),   MakeField("r10", &W::R10),
                           MakeField("r11", &W::R11),       MakeField("r12", &W::R12), MakeField("r13", &W::R13),
                           MakeField("r14", &W::R14),       MakeField("r15", &W::R15) };
    }
};

template <>
struct ReplicationCodec<EquipmentState>
{
    using Wire = EquipmentStateWire;
    static void ToWire(const ReplicationWireContext& context, const EquipmentState& state, EquipmentStateWire& wire);
    static void FromWire(const ReplicationWireContext& context, const EquipmentStateWire& wire, EquipmentState& state);
};
