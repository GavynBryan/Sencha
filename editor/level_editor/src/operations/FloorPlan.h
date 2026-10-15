#pragma once

#include "brush/BrushSection.h"

#include <ecs/EntityId.h>
#include <math/Vec.h>

#include <cstdint>
#include <optional>
#include <vector>

class EditorDocument;

// A level cut by a horizontal plane, on the ground plane: x is world X and y
// is world Z, so north (-Z) is negative y.
struct FloorPlan
{
    struct Brush
    {
        EntityId Entity;
        std::vector<BrushSectionContour> Contours;
    };
    struct Point
    {
        EntityId Entity;
        Vec2d Position;
    };

    float Height = 0.0f;
    std::vector<Brush> Brushes;
    // Entities with a place but no extent: spawns, lights, cameras.
    std::vector<Point> Points;
    Vec2d Min;
    Vec2d Max;
    bool Empty = true;
};

// Every visible brush cut at `height`, in entity order, keeping only brushes
// the plane meets; every point entity, whatever its height.
[[nodiscard]] FloorPlan SectionLevel(const EditorDocument& document, float height);

struct FloorPlanImage
{
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::vector<std::uint8_t> Rgba;
    // Pixel (0, 0)'s top-left corner on the ground plane, and pixels per metre.
    Vec2d Origin;
    float Scale = 0.0f;
};

// North up, east right: a metre grid, cut solids filled dark, open runs drawn as
// lines, point entities as dots. `pixelsPerMetre` shrinks until the longer side
// fits `maxSide`. Nullopt for an empty plan.
[[nodiscard]] std::optional<FloorPlanImage> RasterizeFloorPlan(const FloorPlan& plan,
                                                               float pixelsPerMetre,
                                                               std::uint32_t maxSide);
