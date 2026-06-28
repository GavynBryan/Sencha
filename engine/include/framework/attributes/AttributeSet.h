#pragma once

#include <ecs/ComponentTypeId.h>
#include <framework/attributes/AttributeId.h>

#include <cstdint>
#include <type_traits>

//=============================================================================
// AttributeSet
//
// Per-entity attribute values as a trivially-copyable ECS component: a fixed-cap,
// id-sorted block of (Base, Current). Base is authoritative — set by authoring
// and instant effects. Current is derived each tick by ResolveAttributes (Base
// folded with active modifiers, then clamped). Attribute *definitions* live in
// AttributeRegistry.
//=============================================================================
struct AttributeSet
{
    static constexpr std::uint8_t Capacity = 16;

    AttributeId Ids[Capacity];
    float Base[Capacity];
    float Current[Capacity];
    std::uint8_t Count = 0;

    // Add a new attribute slot with base value (Current starts equal to base).
    // Returns true if newly added; false if already present, invalid, or full
    // (asserts in debug on overflow).
    bool Add(AttributeId id, float base);

    [[nodiscard]] bool Has(AttributeId id) const;

    // Set the base value of an existing attribute; returns false if absent.
    bool SetBase(AttributeId id, float base);

    [[nodiscard]] float GetBase(AttributeId id, float fallback = 0.0f) const;
    [[nodiscard]] float GetCurrent(AttributeId id, float fallback = 0.0f) const;

    // Direct slot access for the resolve / effect-fold passes; nullptr if the
    // attribute is not in this set.
    [[nodiscard]] float* BasePtr(AttributeId id);
    [[nodiscard]] float* CurrentPtr(AttributeId id);

    void Clear() { Count = 0; }
    [[nodiscard]] std::uint8_t Size() const { return Count; }
    [[nodiscard]] bool Empty() const { return Count == 0; }
};

static_assert(std::is_trivially_copyable_v<AttributeSet>,
              "AttributeSet must be trivially copyable to live in ECS chunks");

SENCHA_DECLARE_COMPONENT_TYPE(AttributeSet, "sencha.attribute_set");
