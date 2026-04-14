#pragma once

#include <assets/Image.h>

#include <optional>
#include <string_view>

//=============================================================================
// LoadImageFromFile
//
// Reads an image from disk via stb_image and returns it as a CPU-side Image
// with 4 bytes-per-pixel RGBA output, regardless of the source channel count.
//
// `srgb` controls whether the resulting Image is tagged RGBA8_SRGB (true,
// default -- for color maps / sprites) or RGBA8 (false -- for data maps).
// It affects only the PixelFormat tag; the raw byte values are identical.
//
// Returns nullopt if the file cannot be opened or decoded. The caller should
// log the failure if needed -- this function is intentionally silent.
//=============================================================================
[[nodiscard]] std::optional<Image> LoadImageFromFile(std::string_view path, bool srgb = true);

// Decode an image from an in-memory buffer (e.g. embedded assets, network data).
// Same channel-forcing and srgb semantics as LoadImageFromFile.
[[nodiscard]] std::optional<Image> LoadImageFromMemory(const uint8_t* data, int byteLen, bool srgb = true);
