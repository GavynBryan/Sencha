#pragma once

#include <core/serialization/BinaryReader.h>
#include <core/serialization/BinaryWriter.h>
#include <core/serialization/FourCC.h>
#include <math/spatial/GridTransform3d.h>

#include <cstdint>
#include <span>
#include <vector>

//=============================================================================
// ProbeVolumeFormat (.sprobe)
//
// Chunked binary container for a zone's baked irradiance probe volumes:
// BinaryHeader ('SPRB', version), then a 'VOLS' volume table, then per-volume
// payload chunks. Every payload chunk starts with the u32 index of the volume
// it belongs to, so chunk order is free and unknown chunk ids skip
// forward-compatibly (the .smesh / 'SCNE' practice).
//
// SH payload layout ('SHL1'): after the volume index, three channel planes
// (R, G, B), each PointCount() quads of fp16 in coefficient order
// (c1x, c1y, c1z, c0) and Grid3d point order. That is exactly one 3D-texture
// upload per channel, no repacking at load.
//
// Validity ('VLDT'): after the volume index, one bit per probe (LSB-first),
// set = the probe baked valid (not dilated). Diagnostics only: the runtime
// ignores this chunk; the editor overlay tints dilated probes with it.
//=============================================================================

inline constexpr std::uint32_t kProbeFileMagic = MakeFourCC('S', 'P', 'R', 'B');
inline constexpr std::uint32_t kProbeFormatVersion = 1;

inline constexpr std::uint32_t kProbeChunkVolumeTable = MakeFourCC('V', 'O', 'L', 'S');
inline constexpr std::uint32_t kProbeChunkShL1 = MakeFourCC('S', 'H', 'L', '1');
inline constexpr std::uint32_t kProbeChunkValidity = MakeFourCC('V', 'L', 'D', 'T');

// Coefficient quads per probe per channel (L1 spherical harmonics).
inline constexpr std::uint32_t kProbeShCoefficients = 4;
inline constexpr std::uint32_t kProbeShChannels = 3;

struct ProbeVolumeRecord
{
    GridTransform3d Grid;
    // Selection precedence when volumes overlap: higher wins, then smaller
    // volume, then lower StableIndex. StableIndex is the volume's position in
    // cook order, so precedence cannot flicker between runs or reloads.
    std::int32_t Priority = 0;
    std::uint32_t StableIndex = 0;
    // fp16, kProbeShChannels planes of PointCount() * kProbeShCoefficients.
    std::vector<std::uint16_t> ShHalf;
    // One bit per probe, LSB-first; empty when the file carried no chunk.
    std::vector<std::uint8_t> ValidityBits;
};

struct ProbeVolumeFile
{
    std::vector<ProbeVolumeRecord> Volumes;
};

[[nodiscard]] bool WriteProbeVolumeFile(BinaryWriter& writer, const ProbeVolumeFile& file);
[[nodiscard]] bool ReadProbeVolumeFile(BinaryReader& reader, ProbeVolumeFile& file);

// Deterministic float -> fp16 with round-to-nearest-even, and its inverse.
// The bake quantizes through these so double-cook byte equality holds.
std::uint16_t FloatToHalf(float value);
float HalfToFloat(std::uint16_t value);

// One bit per input byte (nonzero = set), LSB-first within each output byte.
std::vector<std::uint8_t> PackValidityBits(std::span<const std::uint8_t> flags);
bool ValidityBitSet(std::span<const std::uint8_t> bits, std::uint32_t index);
