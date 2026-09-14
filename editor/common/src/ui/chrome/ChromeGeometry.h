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
    Grid,       // faint blueprint grid over an empty well
    Bolt,       // the screw's heavier sibling, for bar caps and the primary frame
    LightStrip, // a short illuminated bar mounted on a frame's ring
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

// A bar's chassis: the band a menu bar, toolbar, or status bar is built from.
// Read top to bottom, a bar is a rim, a recessed channel, and a rim; the
// controls sit on a lane centered in the channel, and a cap terminates each
// end of the channel.
struct BarSpec
{
    float Rim = 0.0f;       // depth of the metal band along the top and bottom edges
    float Clearance = 0.0f; // channel floor above and below the control lane
    float Cap = 0.0f;       // width of the block that terminates each end; 0 = no caps
};

struct BarRects
{
    ImVec2 TopRimMin, TopRimMax;
    ImVec2 BottomRimMin, BottomRimMax;
    ImVec2 ChannelMin, ChannelMax; // between the rims and inside the caps
    ImVec2 LeftCapMin, LeftCapMax;
    ImVec2 RightCapMin, RightCapMax;
    ImVec2 LaneMin, LaneMax; // where controls are seated: `itemHeight` tall, centered in the channel
    bool HasCaps = false;
};

// The chassis of the bar spanning [mn, mx] hosting controls `itemHeight` tall.
// The rims are clamped so the channel never inverts, the caps are dropped when
// the bar is too narrow to carry them, and the lane is snapped to whole pixels
// so the 1px bevels it sits between stay crisp.
BarRects BarLayout(ImVec2 mn, ImVec2 mx, const BarSpec& spec, float itemHeight);

// The bar height that gives `itemHeight` controls their full clearance: a bar
// this tall lays out with a lane exactly `itemHeight` tall at Rim + Clearance.
float BarHeightFor(const BarSpec& spec, float itemHeight);

// How a bar row placed its center block.
enum class BarRowFit : std::uint8_t
{
    Centered, // the center block sits on the bar's midpoint
    Shifted,  // an outer block forced it off the midpoint, but nothing overlaps
    // It did not fit between the outer blocks. Those keep their ends and the
    // center starts after the left one, overlapping the right one: the signal
    // to drop a block and lay the row out again.
    Flowed,
};

struct BarRowRects
{
    float LeftMin = 0.0f, LeftMax = 0.0f;
    float CenterMin = 0.0f, CenterMax = 0.0f;
    float RightMin = 0.0f, RightMax = 0.0f;
    // The runs left over between the blocks; Min == Max when a run collapsed.
    float LeftFreeMin = 0.0f, LeftFreeMax = 0.0f;
    float RightFreeMin = 0.0f, RightFreeMax = 0.0f;
    BarRowFit Fit = BarRowFit::Centered;
};

// The bar chassis's three-block row: a block at the channel's left end, one at
// its right end, and one centered on the channel's midpoint, shifted only as
// far as the outer blocks and `gap` force it.
//
// Three blocks, one axis, fixed widths in and screen x out. It is the rule this
// chassis needs, not a layout engine: a row that does not fit these three
// blocks wants its own function or its own placement, not a mode added here.
BarRowRects BarRowLayout(float channelMin, float channelMax, float leftWidth, float centerWidth,
                         float rightWidth, float gap);

// The upper UV for tiling a `texture`-sized image across a `region`-sized area
// with a repeating sampler: how many times it fits on each axis. A degenerate
// texture tiles once rather than dividing by zero.
ImVec2 SurfaceTileUv(ImVec2 region, ImVec2 texture);

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

// Ornaments mounted on a frame's metal ring rather than inside its well, for a
// composition whose body covers the well (a viewport's scene image paints over
// anything there). Places a bolt just past each corner chamfer, a lit strip
// inside each bottom corner, and a vent centred on the top ring.
//
// Parts are placed in that order, so a frame -- or an `out` span -- with room
// for only some of them keeps the bolts and loses the vent first. Returns how
// many were placed. Everything lands inside the ring band and nothing inside
// the well.
int LayoutRingOrnaments(ImVec2 mn, ImVec2 mx, float ring, float chamfer, float boltRadius,
                        float ventLength, float stripLength, float gap, std::span<OrnamentSlot> out);

// Width the rail's line leaves free at its right end for the tier's rail
// ornaments (what LayoutOrnaments will place there when the rail is wide
// enough). `ledDiameter` is twice the screw radius.
float RailOrnamentWidth(OrnamentTier tier, float ventLength, float slashLength, float ledDiameter, float gap);
} // namespace EditorChrome
