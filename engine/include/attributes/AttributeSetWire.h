#pragma once

#include <attributes/AttributeSet.h>
#include <core/metadata/TypeSchema.h>
#include <net/ReplicationLayout.h>

#include <cstdint>

// An attribute by its name's wire key, with its values.
struct AttributeWire
{
    std::uint32_t Key = 0;
    float Base = 0.0f;
    float Current = 0.0f;
};

template <>
struct TypeSchema<AttributeWire>
{
    static constexpr std::string_view Name = "attribute_wire";
    static auto Fields()
    {
        return std::tuple{ MakeField("key", &AttributeWire::Key).OwnerOnly(),
                           MakeField("base", &AttributeWire::Base).OwnerOnly(),
                           MakeField("current", &AttributeWire::Current).OwnerOnly() };
    }
};

// An actor's attributes go to its owner alone, who predicts with them, in
// wire-key order, which every machine computes alike from the same attributes.
template <>
struct ReplicationCodec<AttributeSet>
{
    using Wire = ReplicationNoHead;
    using Lists = std::tuple<ReplicatedListOf<AttributeWire, AttributeSet::Capacity>>;
    static constexpr std::string_view ListNames[] = { "attributes" };
    static void ToWire(const ReplicationWireContext& context, const AttributeSet& set, ReplicationImage& wire);
    static void FromWire(const ReplicationWireContext& context, const ReplicationImage& wire, AttributeSet& set);
};
