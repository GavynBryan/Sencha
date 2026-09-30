#pragma once

#include <cstdint>

// How strongly an offered action speaks, whatever presents it.
enum class ActionTone : std::uint8_t
{
    Normal,      // an ordinary action
    Active,      // a toggle that is on, or the action a step is waiting for
    Destructive, // removes something
};
