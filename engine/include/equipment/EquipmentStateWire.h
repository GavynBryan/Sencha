#pragma once

#include <core/metadata/ScalarGroup.h>
#include <core/metadata/TypeSchema.h>
#include <equipment/EquipmentComponents.h>
#include <net/ReplicationLayout.h>

#include <cstdint>

inline constexpr std::size_t kEquipmentSourcesPerWireRun = 2;
static_assert(kEquipmentRecordCapacity == 32, "EquipmentStateWire names one member per record.");

// One record: its key, its definition's wire key, its slots, its primary slot. A
// run of its own, so a change to one record resends that record alone.
struct EquipmentWireWords
{
    std::uint32_t Values[4] = {};
};

// The sources of two neighbouring records, their owner's to see.
struct EquipmentWireSources
{
    std::uint64_t Values[kEquipmentSourcesPerWireRun] = {};
};

struct EquipmentStateWire
{
    std::uint32_t Layout = 0;
    EquipmentWireWords R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15;
    EquipmentWireWords R16, R17, R18, R19, R20, R21, R22, R23, R24, R25, R26, R27, R28, R29, R30, R31;
    EquipmentWireSources S0, S1, S2, S3, S4, S5, S6, S7, S8, S9, S10, S11, S12, S13, S14, S15;
};

template <>
struct PackedScalarGroup<EquipmentWireWords>
{
    static constexpr std::size_t Count = 4;
    using Component = std::uint32_t;
};

template <>
struct PackedScalarGroup<EquipmentWireSources>
{
    static constexpr std::size_t Count = kEquipmentSourcesPerWireRun;
    using Component = std::uint64_t;
};

template <>
struct TypeSchema<EquipmentStateWire>
{
    static constexpr std::string_view Name = "equipment_state_wire";
    static auto Fields()
    {
        using W = EquipmentStateWire;
        return std::tuple{ MakeField("layout", &W::Layout), MakeField("r0", &W::R0), MakeField("r1", &W::R1),
                           MakeField("r2", &W::R2), MakeField("r3", &W::R3), MakeField("r4", &W::R4),
                           MakeField("r5", &W::R5), MakeField("r6", &W::R6), MakeField("r7", &W::R7),
                           MakeField("r8", &W::R8), MakeField("r9", &W::R9), MakeField("r10", &W::R10),
                           MakeField("r11", &W::R11), MakeField("r12", &W::R12), MakeField("r13", &W::R13),
                           MakeField("r14", &W::R14), MakeField("r15", &W::R15), MakeField("r16", &W::R16),
                           MakeField("r17", &W::R17), MakeField("r18", &W::R18), MakeField("r19", &W::R19),
                           MakeField("r20", &W::R20), MakeField("r21", &W::R21), MakeField("r22", &W::R22),
                           MakeField("r23", &W::R23), MakeField("r24", &W::R24), MakeField("r25", &W::R25),
                           MakeField("r26", &W::R26), MakeField("r27", &W::R27), MakeField("r28", &W::R28),
                           MakeField("r29", &W::R29), MakeField("r30", &W::R30), MakeField("r31", &W::R31),
                           MakeField("s0", &W::S0).OwnerOnly(), MakeField("s1", &W::S1).OwnerOnly(),
                           MakeField("s2", &W::S2).OwnerOnly(), MakeField("s3", &W::S3).OwnerOnly(),
                           MakeField("s4", &W::S4).OwnerOnly(), MakeField("s5", &W::S5).OwnerOnly(),
                           MakeField("s6", &W::S6).OwnerOnly(), MakeField("s7", &W::S7).OwnerOnly(),
                           MakeField("s8", &W::S8).OwnerOnly(), MakeField("s9", &W::S9).OwnerOnly(),
                           MakeField("s10", &W::S10).OwnerOnly(), MakeField("s11", &W::S11).OwnerOnly(),
                           MakeField("s12", &W::S12).OwnerOnly(), MakeField("s13", &W::S13).OwnerOnly(),
                           MakeField("s14", &W::S14).OwnerOnly(), MakeField("s15", &W::S15).OwnerOnly() };
    }
};

template <>
struct ReplicationCodec<EquipmentState>
{
    using Wire = EquipmentStateWire;
    static void ToWire(const ReplicationWireContext& context, const EquipmentState& state, EquipmentStateWire& wire);
    static void FromWire(const ReplicationWireContext& context, const EquipmentStateWire& wire, EquipmentState& state);
};
