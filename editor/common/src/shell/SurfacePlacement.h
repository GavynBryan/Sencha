#pragma once

#include <math/Vec.h>
#include <math/geometry/2d/Rect2d.h>

#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>

// Where a surface of `home`'s size opens: at `home` when nothing in `taken`
// overlaps it, else at the free place nearest home on a `step` grid inside
// `bounds`, else at home over whatever is there.
[[nodiscard]] Rect2d FindSpace(const Rect2d& home, std::span<const Rect2d> taken, const Rect2d& bounds, float step);

// Which way something opened beside `anchor` goes for the more of the window:
// leftward from an anchor right of its middle, upward from one below it.
struct OpeningToward
{
    bool Left = false;
    bool Up = false;
};
[[nodiscard]] OpeningToward OpenTowardRoom(const Rect2d& anchor, Vec2d window);

// The offset nearest `offset` that keeps `placed` moved by it inside `bounds`.
// A surface larger than the bounds keeps its top left edge inside them.
[[nodiscard]] Vec2d KeepInside(Vec2d offset, const Rect2d& placed, const Rect2d& bounds);

// How a surface finds its place until the person moves it: a summoned one opens
// in free space and stays there; an anchored one keeps to its home as that
// moves (a card whose height follows its content, a window that resizes).
enum class SurfaceAnchor : std::uint8_t
{
    FreeSpace,
    Home,
};

//=============================================================================
// SurfaceLayout
// Where the shell's floating surfaces are, one key per surface and shape. A
// surface keeps for the session where the person moved or sized it, and nothing
// here resizes one on its own.
//=============================================================================
class SurfaceLayout
{
public:
    enum class Phase : std::uint8_t
    {
        Resting,
        // Pressed on a header or a corner; it changes once the pointer moves.
        PressedHeader,
        PressedCorner,
        Moving,
        Sizing,
    };

    // Where `key` is while it shows, kept inside `bounds` and taking `home`'s
    // size until the person sizes it. Until they move it, a FreeSpace surface
    // opens in free space from `home` the first frame it shows and stays
    // there; a Home surface sits at `home`.
    Rect2d Place(std::string_view key, SurfaceAnchor anchor, const Rect2d& home, std::span<const Rect2d> taken,
                 const Rect2d& bounds, float step);
    // Not showing this frame: an opening the person never moved or sized is
    // forgotten, so it finds space again next time.
    void Hide(std::string_view key);

    void PressHeader(std::string key, Vec2d pointer);
    void PressCorner(std::string key, Vec2d pointer);
    // The pressed surface follows the pointer's whole travel since the press,
    // kept inside `bounds` and no smaller than `minimum`.
    void Drag(Vec2d pointer, const Rect2d& bounds, Vec2d minimum);
    void Drop();
    // Back to where and how large the shell puts it.
    void Reset(std::string_view key);

    [[nodiscard]] Phase State() const { return Now; }
    // Whether the person chose this surface's size.
    [[nodiscard]] bool Sized(std::string_view key) const;

private:
    struct Entry
    {
        Rect2d Rect;
        bool Moved = false;
        bool Resized = false;
        bool Showing = false;
    };

    std::map<std::string, Entry, std::less<>> Entries;
    Phase Now = Phase::Resting;
    std::string PressedKey;
    Vec2d PressedAt;
    Rect2d RectAtPress;
};
