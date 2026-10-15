#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Encodes tightly packed 8-bit RGBA rows, top row first, as a PNG file's bytes.
// Empty when the size and the pixel count disagree or encoding fails. Writing
// the bytes anywhere is the caller's business.
[[nodiscard]] std::vector<std::byte> EncodePngRgba8(std::uint32_t width, std::uint32_t height,
                                                    std::span<const std::uint8_t> rgba);
