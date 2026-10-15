#pragma once

#include <attributes/AttributeSet.h>
#include <core/metadata/ScalarGroup.h>
#include <core/metadata/TypeSchema.h>
#include <net/ReplicationLayout.h>

#include <cstdint>

// Each attribute by its name's wire key.
struct AttributeSetWireKeys
{
    std::uint32_t Values[AttributeSet::Capacity] = {};
};

struct AttributeSetWireValues
{
    float Values[AttributeSet::Capacity] = {};
};

// An actor's attributes go to its owner alone, who predicts with them.
struct AttributeSetWire
{
    AttributeSetWireKeys Keys;
    AttributeSetWireValues Base;
    AttributeSetWireValues Current;
};

template <>
struct PackedScalarGroup<AttributeSetWireKeys>
{
    static constexpr std::size_t Count = AttributeSet::Capacity;
    using Component = std::uint32_t;
};

template <>
struct PackedScalarGroup<AttributeSetWireValues>
{
    static constexpr std::size_t Count = AttributeSet::Capacity;
    using Component = float;
};

template <>
struct TypeSchema<AttributeSetWire>
{
    static constexpr std::string_view Name = "attribute_set_wire";
    static auto Fields()
    {
        return std::tuple{ MakeField("keys", &AttributeSetWire::Keys).OwnerOnly(),
                           MakeField("base", &AttributeSetWire::Base).OwnerOnly(),
                           MakeField("current", &AttributeSetWire::Current).OwnerOnly() };
    }
};

template <>
struct ReplicationCodec<AttributeSet>
{
    using Wire = AttributeSetWire;
    static void ToWire(const ReplicationWireContext& context, const AttributeSet& set, AttributeSetWire& wire);
    static void FromWire(const ReplicationWireContext& context, const AttributeSetWire& wire, AttributeSet& set);
};
