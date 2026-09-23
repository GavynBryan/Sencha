#pragma once

#include <cstdint>

// Only Value writes a result.
enum class AuthoredQueryStatus : std::uint8_t
{
    Value,
    // Does not apply here, e.g. the entity lacks the component. Never a default.
    Unavailable,
    // Wrong count, wrong kind, or outside a declared range.
    InvalidArguments,
    // No implementation, or one bound against an older contract.
    Unbound,
    // The handle is from another catalog or an older contract.
    Stale,
    // The implementation's answer violates its own declared result.
    InvalidResult,
};

[[nodiscard]] const char* AuthoredQueryStatusName(AuthoredQueryStatus status);
