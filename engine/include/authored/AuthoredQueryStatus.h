#pragma once

#include <cstdint>

//=============================================================================
// AuthoredQueryStatus
//
// How a question was answered. Only Value writes a result; everything else is
// a distinct reason there is none, so a condition never has to read "false"
// as "this entity has no door".
//=============================================================================
enum class AuthoredQueryStatus : std::uint8_t
{
    // Answered. The result holds a value of the declared result shape.
    Value,
    // The question does not apply here -- the entity has no such component, or
    // the implementation had nothing to say. A consumer decides what that
    // means for it; it is never silently a default.
    Unavailable,
    // The arguments were the wrong count, the wrong kind, or outside a
    // declared range.
    InvalidArguments,
    // Nothing answers this query, or what does was bound against a contract
    // that has since changed.
    Unbound,
    // The query is not live in this catalog, or the caller was compiled
    // against an older revision of it.
    Stale,
};

[[nodiscard]] const char* AuthoredQueryStatusName(AuthoredQueryStatus status);
