#pragma once

#include <cstdint>

// Whether a tool has drafted work waiting on Apply or Cancel, and whether it
// could apply now.
enum class ToolDraft : std::uint8_t
{
    None,       // nothing drafted
    Incomplete, // drafted, but not something that can apply yet
    Ready,      // Apply would commit it
};
