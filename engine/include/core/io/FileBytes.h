#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

// Reads a whole file into `out` as bytes. False when the file cannot be
// opened or fully read; `out` is unspecified then. An empty file reads as
// true with an empty vector. The one implementation of a loop four callers
// had each written for themselves.
[[nodiscard]] bool ReadFileBytes(const std::filesystem::path& path,
                                 std::vector<std::byte>& out);
