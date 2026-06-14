#pragma once

#include <cstdint>

constexpr std::uint32_t MakeFourCC(char a, char b, char c, char d)
{
    return static_cast<std::uint32_t>(static_cast<unsigned char>(a))
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24);
}
