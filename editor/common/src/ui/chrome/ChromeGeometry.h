#pragma once

#include <imgui.h>

#include <cstdint>
#include <span>

// The chrome's pure geometry: chamfered silhouettes, frame and header layout,
// ornament placement. Plain math over ImVec2 with no ImGui runtime, so every
// rule that decides where chrome goes is unit-tested without a context. All
// inputs and outputs are screen pixels (already scaled).
namespace EditorChrome
{
enum class OrnamentKind : std::uint8_t
{
    Screw,
    Vent,
    TripleSlash,
    StatusLed,
    Groove,
    Seam,
    CyanStrip,
    Grid, // faint blueprint grid over an empty well; keep last, it bounds the table
};

// How much decoration a surface can carry, decided from its size so a small
// panel keeps a plain frame and a large one gets corners, vents, and screws.
enum class OrnamentTier : std::uint8_t
{
    Small,
    Medium,
    Large,
};

// A frame's dimensions.
struct FrameSpec
{
    float Chamfer = 0.0f; // clipped-corner size of the outer silhouette
    float Border = 0.0f;  // edge line width
    float Recess = 0.0f;  // metal ring width between the outer edge and the content well
    float Rail = 0.0f;    // header rail height inside the well; 0 = no rail
};

// A rectangle with its corners clipped at 45 degrees: 8 points clockwise from
// the top edge's left end, or 4 when the chamfer is zero. Count is 0 for a
// degenerate rectangle. The chamfer is clamped to half the shorter side.
struct ChamferPoly
{
    ImVec2 P[8];
    int Count = 0;
};
ChamferPoly ChamferOutline(ImVec2 mn, ImVec2 mx, float chamfer);

// The band just inside the top-left chamfer of [mn, mx]: `thickness` deep,
// parallel to the cut, 4 points clockwise from the left edge. Count is 0 when
// there is no chamfer or no room; the band never reaches past half the
// shorter side.
ChamferPoly CornerWedge(ImVec2 mn, ImVec2 mx, float chamfer, float thickness);

// A parallelogram over [mn, mx] leaning `lean` to the right at the top, 4
// points clockwise from the top edge. The lean is clamped to half the width.
ChamferPoly SlantedCap(ImVec2 mn, ImVec2 mx, float lean);

// The rectangles a frame is built from: the content well inside the metal ring
// and the header rail across the well's top (zero height when the spec has no
// rail).
struct FrameRects
{
    ImVec2 WellMin;
    ImVec2 WellMax;
    ImVec2 RailMin;
    ImVec2 RailMax;
};
FrameRects FrameLayout(ImVec2 mn, ImVec2 mx, const FrameSpec& spec);

struct ReadoutRects
{
    ImVec2 Size;
    ImVec2 LabelMin, ValueMin;
    ImVec2 LedMin, LedMax;
    bool HasLed = false;
};
ReadoutRects ReadoutLayout(ImVec2 mn, float labelWidth, float valueWidth, float height,
                           float padding, float gap, float ledSize);

struct TileRects
{
    ImVec2 FaceMin, FaceMax;
    ImVec2 LabelMin, LabelMax;
    ImVec2 BadgeMin, BadgeMax;
    ImVec2 Size;
};
TileRects TileLayout(ImVec2 mn, float size, float labelHeight, float badgeSize, float inset);

// Corner bracket segments; length and thickness are constrained by the rect.
struct BracketLines
{
    ImVec2 Points[16];
    int Count = 0;
};
BracketLines BracketCorners(ImVec2 mn, ImVec2 mx, float length);

// A header's regions, left to right: accent cap, title, stretchable line,
// right ornament, control (close/menu). When the width runs out the regions
// yield in the order line, ornament, title, cap; the control region is kept
// first, since it is the one a user clicks.
struct HeaderRegions
{
    ImVec2 CapMin, CapMax;
    ImVec2 TitleMin, TitleMax;
    ImVec2 LineMin, LineMax;
    ImVec2 OrnamentMin, OrnamentMax;
    ImVec2 ControlMin, ControlMax;
    bool HasCap = false;
    bool HasTitle = false;
    bool HasLine = false;
    bool HasOrnament = false;
    bool HasControl = false;
};
HeaderRegions LayoutHeader(ImVec2 mn, ImVec2 mx, float capWidth, float titleWidth,
                           float ornamentWidth, float controlWidth, float gap);

// The tier for a surface of `size`: Large from largeMin up, Medium from
// mediumMin up, Small below, judged on the shorter side.
OrnamentTier TierFor(ImVec2 size, float mediumMin, float largeMin);

struct OrnamentSlot
{
    OrnamentKind Kind;
    ImVec2 Min;
    ImVec2 Max;
};

// Places a frame's ornaments for its tier into `out` and returns how many were
// placed (never more than out.size()). Small places nothing. Medium mounts a
// screw in each bottom corner of the well and a status LED (a dot of
// `screwRadius`) at the right end of the rail. Large adds a vent and a triple
// slash left of the LED. A rail too short for the Large set falls back to the
// LED alone; one too short for that carries nothing. Placement is a pure
// function of the rects, so it never moves between frames.
int LayoutOrnaments(const FrameRects& rects, OrnamentTier tier, float screwRadius,
                    float ventLength, float slashLength, float gap, std::span<OrnamentSlot> out);

// Width the rail's line leaves free at its right end for the tier's rail
// ornaments (what LayoutOrnaments will place there when the rail is wide
// enough). `ledDiameter` is twice the screw radius.
float RailOrnamentWidth(OrnamentTier tier, float ventLength, float slashLength, float ledDiameter, float gap);
} // namespace EditorChrome
