#pragma once

#include <cstdint>

constexpr std::uint32_t MakeFourCC(char a, char b, char c, char d)
{
    return static_cast<std::uint32_t>(static_cast<unsigned char>(a))
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24);
}

constexpr std::uint32_t SceneMagic = MakeFourCC('S', 'C', 'N', 'E');
constexpr std::uint32_t SceneVersion = 1;

//=============================================================================
// SceneChunk
//
// Four-character chunk identifiers written into the binary scene stream.
// Unknown chunk IDs are skipped during load for forward compatibility.
//=============================================================================
namespace SceneChunk
{
    constexpr std::uint32_t Registry = MakeFourCC('R', 'E', 'G', 'V');
    constexpr std::uint32_t Transforms = MakeFourCC('X', 'F', 'R', 'M');
    constexpr std::uint32_t MeshRenders = MakeFourCC('M', 'E', 'S', 'H');
    constexpr std::uint32_t Cameras = MakeFourCC('C', 'A', 'M', 'R');
    constexpr std::uint32_t Hierarchy = MakeFourCC('H', 'I', 'E', 'R');
}
