#pragma once

#include "icons/IconId.h"

#include <math/Vec.h>

#include <cstdint>
#include <string>
#include <vector>

class RadialMenuSession;
struct IRadialMenuModel;

// One glyph on a wheel's face, in a round recess at `Centre` within its petal.
struct RadialMenuKey
{
    Vec2d Centre = {};
    float Radius = 0.0f;
    std::string Label;
    IconId Icon = IconId::None;
    bool Hot = false;     // under the pointer: what a release would choose
    bool Active = false;  // the entry, or the variant, in effect now
    bool Variant = false; // on the outer ring, one of the hot entry's variants
    bool Word = false;    // no glyph: the entry is its label, set across the petal
    std::string Image = {}; // a host image over the word, such as a swatch
};

// What a sector of the wheel is cut from.
enum class RadialMenuPart : std::uint8_t
{
    Plate,    // the dark body everything is set into
    Rim,      // the metal band closing a ring
    Petal,
    HotPetal, // the petal a release would choose, outlined
    Mark,     // the in-effect entry's arc on a rim
    Detail,   // the thin accent arcs along the primary rim
    Hub,      // the recess at the axle
};

// A band about the wheel's centre over `Width` radians clockwise from `Begin`
// (clockwise from straight up); a full turn goes all the way round. Each is
// exactly its own shape: nothing is painted over another to cut it.
struct RadialMenuSector
{
    float Inner = 0.0f;
    float Outer = 0.0f;
    float Begin = 0.0f;
    float Width = 0.0f;
    RadialMenuPart Part = RadialMenuPart::Plate;
};

// What an open wheel shows, in window pixels, taken from the session's layout
// -- the numbers it hit-tests -- and the model's entries. Closed is empty.
struct RadialMenuFace
{
    bool Open = false;
    Vec2d Centre = {};
    float PlateRadius = 0.0f;
    // Back to front.
    std::vector<RadialMenuSector> Sectors;
    // The caption's line: under everything the wheel can show, as wide as it.
    float CaptionTop = 0.0f;
    float CaptionHeight = 0.0f;
    std::string Caption;
    bool CaptionNamesCurrent = false; // nothing is hot: it names what is already on
    std::vector<RadialMenuKey> Keys;
    // What the hub reads out, a line each, within HubRadius of the centre.
    std::vector<std::string> Summary;
    float HubRadius = 0.0f;
};

[[nodiscard]] RadialMenuFace FaceOf(const RadialMenuSession& wheel, const IRadialMenuModel& menu);
