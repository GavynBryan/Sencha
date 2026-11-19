#pragma once

#include <core/metadata/TypeSchema.h>
#include <net/ReplicationLayout.h>
#include <world/held/Held.h>

#include <cstdint>

struct HeldWire
{
    std::uint64_t Holder = 0;
    std::uint32_t Position = 0;
};

template <>
struct TypeSchema<HeldWire>
{
    static constexpr std::string_view Name = "held_wire";
    static auto Fields()
    {
        return std::tuple{ MakeField("holder", &HeldWire::Holder), MakeField("position", &HeldWire::Position) };
    }
};

template <>
struct ReplicationCodec<Held>
{
    using Wire = HeldWire;
    static void ToWire(const ReplicationWireContext& context, const Held& held, HeldWire& wire);
    static void FromWire(const ReplicationWireContext& context, const HeldWire& wire, Held& held);
};
