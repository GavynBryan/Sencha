#pragma once

#include <assets/font/FontFace.h>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

// .sfont round trip. Pure both ways, for the same reasons as the .sui pair:
// the write half runs in an importer that reports rather than logs, and the
// read half may run on a task thread.

[[nodiscard]] bool WriteSfontToBytes(const FontFace& face, std::vector<std::byte>& out);

[[nodiscard]] bool LoadSfontFromBytes(std::span<const std::byte> bytes,
                                      FontFace& out,
                                      std::string* error = nullptr);
