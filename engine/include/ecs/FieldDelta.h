#pragma once

#include <cstdint>
#include <cstring>

// Numeric kind of a field targeted by a deferred delta (`+=`). The script layer
// maps its scalar kind to this when recording; the ECS applies the add without
// any script dependency. Integer adds wrap (two's complement), matching the T VM
// (see t-language-v1-spec: "ni and i64 arithmetic wraps"); float adds are IEEE.
enum class NumericFieldKind : uint8_t
{
    Int32,
    UInt32,
    Int64,
    UInt64,
    Float,
    Double,
};

// Applies `field += delta` in place. `field` and `delta` point at values of the
// given kind (possibly unaligned; access goes through memcpy). Integer overflow
// wraps; this is what makes integer multi-source deltas order-independent.
inline void ApplyNumericDelta(void* field, const void* delta, NumericFieldKind kind)
{
    switch (kind)
    {
    case NumericFieldKind::Int32:
    {
        std::uint32_t a, b;
        std::memcpy(&a, field, 4);
        std::memcpy(&b, delta, 4);
        const std::uint32_t r = a + b; // wraps
        std::memcpy(field, &r, 4);
        break;
    }
    case NumericFieldKind::UInt32:
    {
        std::uint32_t a, b;
        std::memcpy(&a, field, 4);
        std::memcpy(&b, delta, 4);
        const std::uint32_t r = a + b;
        std::memcpy(field, &r, 4);
        break;
    }
    case NumericFieldKind::Int64:
    {
        std::uint64_t a, b;
        std::memcpy(&a, field, 8);
        std::memcpy(&b, delta, 8);
        const std::uint64_t r = a + b; // wraps
        std::memcpy(field, &r, 8);
        break;
    }
    case NumericFieldKind::UInt64:
    {
        std::uint64_t a, b;
        std::memcpy(&a, field, 8);
        std::memcpy(&b, delta, 8);
        const std::uint64_t r = a + b;
        std::memcpy(field, &r, 8);
        break;
    }
    case NumericFieldKind::Float:
    {
        float a, b;
        std::memcpy(&a, field, 4);
        std::memcpy(&b, delta, 4);
        const float r = a + b;
        std::memcpy(field, &r, 4);
        break;
    }
    case NumericFieldKind::Double:
    {
        double a, b;
        std::memcpy(&a, field, 8);
        std::memcpy(&b, delta, 8);
        const double r = a + b;
        std::memcpy(field, &r, 8);
        break;
    }
    }
}
