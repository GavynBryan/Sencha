#pragma once

#include <core/metadata/TypeSchema.h>
#include <logic/Counter.h>
#include <net/ReplicationLayout.h>

#include <cstdint>

struct CounterWire
{
    std::uint32_t Name = 0;
    std::int32_t Value = 0;
};

template <>
struct TypeSchema<CounterWire>
{
    static constexpr std::string_view Name = "counter_wire";
    static auto Fields()
    {
        return std::tuple{ MakeField("name", &CounterWire::Name), MakeField("value", &CounterWire::Value) };
    }
};

template <>
struct ReplicationCodec<Counter>
{
    using Wire = CounterWire;
    static void ToWire(const ReplicationWireContext& context, const Counter& counter, CounterWire& wire);
    static void FromWire(const ReplicationWireContext& context, const CounterWire& wire, Counter& counter);
};
