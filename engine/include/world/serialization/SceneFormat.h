#pragma once

#include <core/serialization/FourCC.h>

#include <cstdint>

constexpr std::uint32_t SceneMagic = MakeFourCC('S', 'C', 'N', 'E');
constexpr std::uint32_t SceneVersion = 1;

//=============================================================================
// SceneChunk
//
// Four-character chunk identifiers written into the binary scene stream.
// Unknown chunk IDs are skipped during load for forward compatibility.
//
// Only the structural chunks live here. Component chunk IDs are declared as
// TypeSchema<T>::SceneChunkId in each component's own header, so everything
// about a component stays in one file.
//=============================================================================
namespace SceneChunk
{
    constexpr std::uint32_t Registry = MakeFourCC('R', 'E', 'G', 'V');
    constexpr std::uint32_t Hierarchy = MakeFourCC('H', 'I', 'E', 'R');
}
