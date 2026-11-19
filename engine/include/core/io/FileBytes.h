#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

// Reads a whole file into `out` as bytes. False when the file cannot be
// opened or fully read; `out` is unspecified then. An empty file reads as
// true with an empty vector.
[[nodiscard]] bool ReadFileBytes(const std::filesystem::path& path,
                                 std::vector<std::byte>& out);

// Writes `bytes` as the whole file, replacing what was there in one step: a
// reader, a crash or a power cut part way through sees the old file or the new
// one. False when it could not; what was there is then intact.
[[nodiscard]] bool WriteFileBytes(const std::filesystem::path& path,
                                  std::span<const std::byte> bytes);
[[nodiscard]] bool WriteFileText(const std::filesystem::path& path, std::string_view text);
