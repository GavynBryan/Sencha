#pragma once

#include <math/Vec.h>
#include <math/geometry/2d/Rect2d.h>

#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>

// How a floating surface makes way for an instrument it would cover: a
// Shrink gives up that edge and keeps the others where they are anchored;
// ShiftRight moves a surface of fixed width whole.
enum class SurfaceYield : std::uint8_t
{
    ShrinkLeft,
    ShrinkRight,
    ShrinkBottom,
    ShiftRight,
};

struct SurfaceObstacle
{
    Rect2d Area;
    SurfaceYield Yield = SurfaceYield::ShrinkLeft;
};

// What a surface gives up from its default place, in surface pixels: margins
// on three edges, and a shift right.
struct SurfaceClearance
{
    float Left = 0.0f;
    float Right = 0.0f;
    float Bottom = 0.0f;
    float ShiftX = 0.0f;

    bool operator==(const SurfaceClearance&) const = default;
};

// The smallest a surface is made by giving way; past it, the surface keeps
// this size and overlaps what it would cover.
struct SurfaceMinimum
{
    float Width = 0.0f;
    float Height = 0.0f;
};

// The clearance that keeps `natural`, a surface's default place, `gap` away
// from every obstacle it overlaps, on the side each obstacle names, without
// shrinking it below `minimum`.
[[nodiscard]] SurfaceClearance ClearOf(const Rect2d& natural, std::span<const SurfaceObstacle> obstacles, float gap,
                                       SurfaceMinimum minimum = {});

// The default place of a surface measured at `measured` (untransformed) while
// `applied` was in effect.
[[nodiscard]] Rect2d NaturalPlace(const Rect2d& measured, const SurfaceClearance& applied);

// The offset nearest `offset` that keeps `placed` moved by it inside `bounds`.
// A surface larger than the bounds keeps its top left edge inside them.
[[nodiscard]] Vec2d KeepInside(Vec2d offset, const Rect2d& placed, const Rect2d& bounds);

// Where a surface the person moved sits: an offset from its default place,
// and the clearance it had when it was first taken hold of, so it keeps its
// size wherever it is put.
struct SurfaceMove
{
    Vec2d Offset;
    SurfaceClearance Clearance;
};

//=============================================================================
// SurfaceMoves
//
// The shell's floating surfaces that the person has dragged, by key -- one
// key per surface and shape, so the tray and the outline, or the card and the
// drawer, are moved separately. A surface with no move sits where the shell's
// placement puts it.
//=============================================================================
class SurfaceMoves
{
public:
    enum class Phase : std::uint8_t
    {
        Resting,
        // Pressed on a surface's grip; it moves once the pointer does.
        Pressed,
        Dragging,
    };

    // The pointer went down on `key`'s grip at `pointer`. `clearance` is what
    // the surface has given up where it sits now, kept while it is moved.
    void Press(std::string key, Vec2d pointer, const SurfaceClearance& clearance);
    // `placed` is the pressed surface's rect without its move; it stays in
    // `bounds`. The pointer's whole travel since the press counts.
    void Drag(Vec2d pointer, const Rect2d& placed, const Rect2d& bounds);
    void Drop();
    // Back to where the shell puts it.
    void Reset(std::string_view key);

    [[nodiscard]] const SurfaceMove* Find(std::string_view key) const;
    [[nodiscard]] Phase State() const { return Now; }

private:
    std::map<std::string, SurfaceMove, std::less<>> Moves;
    Phase Now = Phase::Resting;
    std::string PressedKey;
    SurfaceClearance PressedClearance;
    Vec2d PressedAt;
    Vec2d OffsetAtPress;
};
