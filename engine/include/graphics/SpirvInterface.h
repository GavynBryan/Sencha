#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <span>

struct SpirvInterface
{
    bool Valid = false;
    std::set<std::uint32_t> Inputs;
    std::set<std::uint32_t> Outputs;
    bool InvariantPosition = false;
    std::optional<std::uint32_t> LastPushMemberOffset;
};

[[nodiscard]] SpirvInterface ReadSpirvInterface(std::span<const std::uint32_t> words);
