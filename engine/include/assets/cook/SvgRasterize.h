#pragma once

#include <assets/texture/Image.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

// SVG bytes -> an sRGB RGBA8 image `heightPixels` tall, width following the
// drawing's aspect, straight alpha. Dev-only, compiled under SENCHA_ENABLE_COOK:
// the one rasterizer for authored vector art, which the texture cook uses and a
// dev tool baking icons at startup may call too. Pure; nullopt on bytes that do
// not parse to a drawing with an extent.
[[nodiscard]] std::optional<Image> RasterizeSvg(std::span<const std::byte> svg,
                                                std::uint32_t heightPixels,
                                                std::string* error = nullptr);
