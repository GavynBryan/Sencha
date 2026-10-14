#pragma once

#include <cstdint>
#include <string_view>

// Whether an operation touches simulation state. A cosmetic producer may only
// invoke cosmetic operations, so presentation can never change gameplay. The
// values are persisted by cooked animation clips.
enum class AuthoredScope : std::uint8_t
{
    Cosmetic = 0,
    Gameplay = 1,
};

// Where a predicting client may execute an operation for its own pawn: never,
// wherever presentation runs, or as a journaled state change that replay repeats.
enum class AuthoredPrediction : std::uint8_t
{
    AuthorityOnly,
    Presentation,
    Simulation,
};

[[nodiscard]] std::string_view AuthoredScopeName(AuthoredScope scope);
