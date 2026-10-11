#include "FloorPlan.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/SceneBrushWalk.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
    void Include(FloorPlan& plan, const Vec2d& point)
    {
        if (plan.Empty)
        {
            plan.Min = point;
            plan.Max = point;
            plan.Empty = false;
            return;
        }
        plan.Min = Vec2d{ std::min(plan.Min.X, point.X), std::min(plan.Min.Y, point.Y) };
        plan.Max = Vec2d{ std::max(plan.Max.X, point.X), std::max(plan.Max.Y, point.Y) };
    }

    using Colour = std::array<std::uint8_t, 3>;
    constexpr Colour kPaper{ 244, 244, 240 };
    constexpr Colour kMetreLine{ 226, 226, 222 };
    constexpr Colour kEightMetreLine{ 200, 200, 196 };
    constexpr Colour kSolid{ 52, 54, 60 };
    constexpr Colour kOpenRun{ 196, 60, 48 };
    constexpr Colour kPointMark{ 32, 116, 214 };

    struct Canvas
    {
        FloorPlanImage& Image;

        void Set(std::int64_t column, std::int64_t row, const Colour& colour)
        {
            if (column < 0 || row < 0 || column >= Image.Width || row >= Image.Height)
                return;
            std::uint8_t* pixel =
                &Image.Rgba[(static_cast<std::size_t>(row) * Image.Width + static_cast<std::size_t>(column)) * 4];
            pixel[0] = colour[0];
            pixel[1] = colour[1];
            pixel[2] = colour[2];
            pixel[3] = 255;
        }

        [[nodiscard]] double Column(double x) const { return (x - Image.Origin.X) * Image.Scale; }
        [[nodiscard]] double Row(double y) const { return (y - Image.Origin.Y) * Image.Scale; }
    };

    void DrawGrid(Canvas& canvas)
    {
        const FloorPlanImage& image = canvas.Image;
        // A pixel column or row is a grid line when a whole metre falls inside it.
        for (std::uint32_t column = 0; column < image.Width; ++column)
        {
            const double left = image.Origin.X + column / static_cast<double>(image.Scale);
            const double right = image.Origin.X + (column + 1) / static_cast<double>(image.Scale);
            const double metre = std::ceil(left);
            if (metre >= right)
                continue;
            const bool major = static_cast<std::int64_t>(metre) % 8 == 0;
            for (std::uint32_t row = 0; row < image.Height; ++row)
                canvas.Set(column, row, major ? kEightMetreLine : kMetreLine);
        }
        for (std::uint32_t row = 0; row < image.Height; ++row)
        {
            const double top = image.Origin.Y + row / static_cast<double>(image.Scale);
            const double bottom = image.Origin.Y + (row + 1) / static_cast<double>(image.Scale);
            const double metre = std::ceil(top);
            if (metre >= bottom)
                continue;
            const bool major = static_cast<std::int64_t>(metre) % 8 == 0;
            for (std::uint32_t column = 0; column < image.Width; ++column)
                canvas.Set(column, row, major ? kEightMetreLine : kMetreLine);
        }
    }

    // Even-odd across all of one brush's closed outlines, so its holes stay open.
    void FillSolid(Canvas& canvas, const FloorPlan::Brush& brush)
    {
        const FloorPlanImage& image = canvas.Image;
        std::vector<double> crossings;
        for (std::uint32_t row = 0; row < image.Height; ++row)
        {
            const double y = image.Origin.Y + (row + 0.5) / static_cast<double>(image.Scale);
            crossings.clear();
            for (const BrushSectionContour& contour : brush.Contours)
            {
                if (!contour.Closed)
                    continue;
                const std::size_t count = contour.Points.size();
                for (std::size_t i = 0; i < count; ++i)
                {
                    const Vec2d& a = contour.Points[i];
                    const Vec2d& b = contour.Points[(i + 1) % count];
                    if ((a.Y > y) == (b.Y > y))
                        continue;
                    crossings.push_back(a.X + (y - a.Y) / (b.Y - a.Y) * (b.X - a.X));
                }
            }
            std::ranges::sort(crossings);
            for (std::size_t i = 0; i + 1 < crossings.size(); i += 2)
            {
                const auto first = static_cast<std::int64_t>(std::ceil(canvas.Column(crossings[i]) - 0.5));
                const auto last = static_cast<std::int64_t>(std::floor(canvas.Column(crossings[i + 1]) - 0.5));
                for (std::int64_t column = first; column <= last; ++column)
                    canvas.Set(column, row, kSolid);
            }
        }
    }

    void DrawLine(Canvas& canvas, const Vec2d& from, const Vec2d& to, const Colour& colour)
    {
        const double x0 = canvas.Column(from.X), y0 = canvas.Row(from.Y);
        const double x1 = canvas.Column(to.X), y1 = canvas.Row(to.Y);
        const double steps = std::max({ std::abs(x1 - x0), std::abs(y1 - y0), 1.0 });
        for (double step = 0.0; step <= steps; step += 1.0)
        {
            const double t = step / steps;
            canvas.Set(static_cast<std::int64_t>(std::floor(x0 + (x1 - x0) * t)),
                       static_cast<std::int64_t>(std::floor(y0 + (y1 - y0) * t)), colour);
        }
    }
}

FloorPlan SectionLevel(const EditorDocument& document, float height)
{
    FloorPlan plan;
    plan.Height = height;
    const EditorScene& scene = document.GetScene();

    ForEachVisibleBrushPiece(scene, false,
        [&](EntityId entity, const BrushPiece& piece, const Transform3f& toWorld) {
            std::vector<BrushSectionContour> contours = SectionBrushAtHeight(*piece.Mesh, toWorld, height);
            if (contours.empty())
                return;
            for (const BrushSectionContour& contour : contours)
                for (const Vec2d& point : contour.Points)
                    Include(plan, point);
            if (plan.Brushes.empty() || plan.Brushes.back().Entity != entity)
                plan.Brushes.push_back({ entity, {} });
            for (BrushSectionContour& contour : contours)
                plan.Brushes.back().Contours.push_back(std::move(contour));
        });

    for (const EntityId entity : scene.GetAllEntities())
    {
        if (scene.TryGetBrush(entity) != nullptr || scene.EvaluatedWorldBounds(entity))
            continue;
        const Transform3f* world = scene.TryGetWorldTransform(entity);
        if (world == nullptr)
            continue;
        const Vec2d position{ world->Position.X, world->Position.Z };
        plan.Points.push_back({ entity, position });
        Include(plan, position);
    }
    return plan;
}

std::optional<FloorPlanImage> RasterizeFloorPlan(const FloorPlan& plan, float pixelsPerMetre,
                                                 std::uint32_t maxSide)
{
    if (plan.Empty || pixelsPerMetre <= 0.0f || maxSide == 0)
        return std::nullopt;

    constexpr float kMargin = 1.0f;
    const Vec2d origin{ plan.Min.X - kMargin, plan.Min.Y - kMargin };
    const float spanX = plan.Max.X - plan.Min.X + 2.0f * kMargin;
    const float spanY = plan.Max.Y - plan.Min.Y + 2.0f * kMargin;
    const float scale = std::min(pixelsPerMetre, static_cast<float>(maxSide) / std::max(spanX, spanY));

    FloorPlanImage image;
    image.Origin = origin;
    image.Scale = scale;
    image.Width = std::max(1u, static_cast<std::uint32_t>(std::ceil(spanX * scale)));
    image.Height = std::max(1u, static_cast<std::uint32_t>(std::ceil(spanY * scale)));
    image.Rgba.resize(static_cast<std::size_t>(image.Width) * image.Height * 4);
    for (std::size_t i = 0; i < image.Rgba.size(); i += 4)
    {
        image.Rgba[i + 0] = kPaper[0];
        image.Rgba[i + 1] = kPaper[1];
        image.Rgba[i + 2] = kPaper[2];
        image.Rgba[i + 3] = 255;
    }

    Canvas canvas{ image };
    DrawGrid(canvas);
    for (const FloorPlan::Brush& brush : plan.Brushes)
    {
        FillSolid(canvas, brush);
        for (const BrushSectionContour& contour : brush.Contours)
            if (!contour.Closed)
                for (std::size_t i = 0; i + 1 < contour.Points.size(); ++i)
                    DrawLine(canvas, contour.Points[i], contour.Points[i + 1], kOpenRun);
    }
    for (const FloorPlan::Point& point : plan.Points)
    {
        const auto column = static_cast<std::int64_t>(std::floor(canvas.Column(point.Position.X)));
        const auto row = static_cast<std::int64_t>(std::floor(canvas.Row(point.Position.Y)));
        for (std::int64_t dy = -2; dy <= 2; ++dy)
            for (std::int64_t dx = -2; dx <= 2; ++dx)
                canvas.Set(column + dx, row + dy, kPointMark);
    }
    return image;
}
