#pragma once

#include <assets/texture/Image.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

// SVG bytes -> an sRGB RGBA8 image `heightPixels` tall at the drawing's aspect,
// straight alpha: the one rasterizer for authored vector art (cook-only). Pure;
// nullopt for bytes that are not a drawing with an extent.
[[nodiscard]] std::optional<Image> RasterizeSvg(std::span<const std::byte> svg,
                                                std::uint32_t heightPixels,
                                                std::string* error = nullptr);
