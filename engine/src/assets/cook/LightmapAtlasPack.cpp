#include <assets/cook/LightmapAtlasPack.h>

#include <algorithm>
#include <cmath>

namespace
{
    struct PaddedDims
    {
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
    };

    // Grid-point convention: a chart spanning `extent` world units needs
    // ceil(extent / luxel) + 1 sample points per axis, plus the gutter.
    PaddedDims DimsFor(const Vec2d& extent, float luxel)
    {
        const auto points = [luxel](float span) {
            const float clamped = span < 0.0f ? 0.0f : span;
            return static_cast<std::uint32_t>(std::ceil(clamped / luxel)) + 1u;
        };
        return PaddedDims{ points(extent.X) + 2u * kLightmapGutter,
                           points(extent.Y) + 2u * kLightmapGutter };
    }

    // One deterministic shelf-pack attempt into a fixed atlas. Rects are
    // placed in `order`; the first row/column stay reserved (packing starts
    // at (1,1)). Returns false when anything does not fit.
    bool TryPack(std::span<const PaddedDims> dims,
                 std::span<const std::uint32_t> order,
                 std::uint32_t width, std::uint32_t height,
                 std::vector<LightmapChartRect>& rects)
    {
        std::uint32_t cursorX = 1;
        std::uint32_t cursorY = 1;
        std::uint32_t shelfHeight = 0;
        for (const std::uint32_t index : order)
        {
            const PaddedDims& d = dims[index];
            if (d.Width + 1 > width || d.Height + 1 > height)
                return false;
            if (cursorX + d.Width > width)
            {
                cursorY += shelfHeight;
                cursorX = 1;
                shelfHeight = 0;
            }
            if (cursorY + d.Height > height)
                return false;
            rects[index] = LightmapChartRect{ cursorX, cursorY, d.Width, d.Height };
            cursorX += d.Width;
            shelfHeight = std::max(shelfHeight, d.Height);
        }
        return true;
    }
} // namespace

LightmapAtlasLayout PackLightmapAtlas(std::span<const Vec2d> chartExtents,
                                      float luxelSize,
                                      std::uint32_t maxSize)
{
    LightmapAtlasLayout layout;
    layout.EffectiveLuxelSize = std::max(luxelSize, 1e-4f);
    layout.Rects.resize(chartExtents.size());
    if (chartExtents.empty())
    {
        layout.Width = layout.Height = 4; // minimal atlas: the reserved border alone
        return layout;
    }

    const std::uint32_t cap = std::max<std::uint32_t>(maxSize, 64);
    while (true)
    {
        std::vector<PaddedDims> dims(chartExtents.size());
        for (std::size_t i = 0; i < chartExtents.size(); ++i)
            dims[i] = DimsFor(chartExtents[i], layout.EffectiveLuxelSize);

        std::vector<std::uint32_t> order(chartExtents.size());
        for (std::uint32_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::sort(order.begin(), order.end(), [&dims](std::uint32_t a, std::uint32_t b) {
            if (dims[a].Height != dims[b].Height)
                return dims[a].Height > dims[b].Height;
            if (dims[a].Width != dims[b].Width)
                return dims[a].Width > dims[b].Width;
            return a < b;
        });

        std::uint32_t width = 128;
        std::uint32_t height = 128;
        while (true)
        {
            if (TryPack(dims, order, width, height, layout.Rects))
            {
                layout.Width = width;
                layout.Height = height;
                return layout;
            }
            // Grow the smaller dimension first so the atlas stays near-square.
            if (width <= height && width < cap)
                width *= 2;
            else if (height < cap)
                height *= 2;
            else
                break;
        }

        // Both dimensions capped and still overflowing: coarsen the density
        // and retry. Never multiple pages (one atlas per zone, one bindless
        // index per draw).
        layout.EffectiveLuxelSize *= 1.41421356f;
    }
}
