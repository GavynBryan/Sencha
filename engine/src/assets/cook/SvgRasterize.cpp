#include <assets/cook/SvgRasterize.h>

#include <cmath>
#include <cstring>
#include <vector>

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvg.h>
#include <nanosvgrast.h>

std::optional<Image> RasterizeSvg(std::span<const std::byte> svg, std::uint32_t heightPixels,
                                  std::string* error)
{
    const auto fail = [&](const char* why) -> std::optional<Image> {
        if (error != nullptr)
            *error = why;
        return std::nullopt;
    };
    if (svg.empty() || heightPixels == 0)
        return fail("svg: no bytes or no size");

    // The parser writes into its input, which must end in a terminator.
    std::vector<char> text(svg.size() + 1, '\0');
    std::memcpy(text.data(), svg.data(), svg.size());
    NSVGimage* drawing = nsvgParse(text.data(), "px", 96.0f);
    if (drawing == nullptr)
        return fail("svg: does not parse");
    if (drawing->width <= 0.0f || drawing->height <= 0.0f)
    {
        nsvgDelete(drawing);
        return fail("svg: the drawing has no extent");
    }

    const float scale = static_cast<float>(heightPixels) / drawing->height;
    Image image;
    image.Height = heightPixels;
    image.Width = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::lround(drawing->width * scale)));
    image.Format = PixelFormat::RGBA8_SRGB;
    image.Pixels.assign(static_cast<std::size_t>(image.Width) * image.Height * 4u, 0);

    NSVGrasterizer* rasterizer = nsvgCreateRasterizer();
    nsvgRasterize(rasterizer, drawing, 0.0f, 0.0f, scale, image.Pixels.data(),
                  static_cast<int>(image.Width), static_cast<int>(image.Height),
                  static_cast<int>(image.Width * 4u));
    nsvgDeleteRasterizer(rasterizer);
    nsvgDelete(drawing);
    return image;
}
