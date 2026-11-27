#pragma once

#include <core/metadata/ScalarGroup.h>
#include <core/metadata/TypeSchema.h>
#include <equipment/EquipmentComponents.h>
#include <net/ReplicationLayout.h>

#include <cstdint>

// A record's key, its item's wire key, its slots and its primary slot.
struct EquipmentRecordWireWords
{
    std::uint32_t Values[4] = {};
};

struct EquipmentRecordWire
{
    EquipmentRecordWireWords Words;
    std::uint64_t Source = 0;
};

struct EquipmentStateWireHead
{
    std::uint32_t Layout = 0;
};

template <>
struct PackedScalarGroup<EquipmentRecordWireWords>
{
    static constexpr std::size_t Count = 4;
    using Component = std::uint32_t;
};

template <>
struct TypeSchema<EquipmentRecordWire>
{
    static constexpr std::string_view Name = "equipment_record_wire";
    static auto Fields()
    {
        return std::tuple{ MakeField("words", &EquipmentRecordWire::Words),
                           MakeField("source", &EquipmentRecordWire::Source).OwnerOnly() };
    }
};

template <>
struct TypeSchema<EquipmentStateWireHead>
{
    static constexpr std::string_view Name = "equipment_state_wire_head";
    static auto Fields() { return std::tuple{ MakeField("layout", &EquipmentStateWireHead::Layout) }; }
};

template <>
struct ReplicationCodec<EquipmentState>
{
    using Wire = EquipmentStateWireHead;
    using Lists = std::tuple<ReplicatedListOf<EquipmentRecordWire, kEquipmentRecordCapacity>>;
    static constexpr std::string_view ListNames[] = { "records" };
    static void ToWire(const ReplicationWireContext& context, const EquipmentState& state, ReplicationImage& wire);
    static void FromWire(const ReplicationWireContext& context, const ReplicationImage& wire, EquipmentState& state);
};
