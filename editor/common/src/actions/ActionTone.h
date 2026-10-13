#pragma once

#include <cstdint>
#include <string_view>

// How strongly an offered action speaks, whatever presents it.
enum class ActionTone : std::uint8_t
{
    Normal,      // an ordinary action
    Active,      // a toggle that is on, or the action a step is waiting for
    Destructive, // removes something
};

// What a document styles the tone by.
[[nodiscard]] constexpr std::string_view ActionToneName(ActionTone tone)
{
    switch (tone)
    {
    case ActionTone::Destructive: return "destructive";
    case ActionTone::Active:      return "active";
    case ActionTone::Normal:
    default:                      return "normal";
    }
}
