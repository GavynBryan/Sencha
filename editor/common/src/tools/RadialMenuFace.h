#pragma once

#include "icons/IconId.h"

#include <math/Vec.h>

#include <string>
#include <vector>

class RadialMenuSession;
struct IRadialMenuModel;

// One key on a wheel's face: a round slot at `Centre`.
struct RadialMenuKey
{
    Vec2d Centre = {};
    float Radius = 0.0f;
    std::string Label;
    IconId Icon = IconId::None;
    bool Hot = false;     // under the pointer: what a release would choose
    bool Active = false;  // the entry, or the variant, in effect now
    bool Variant = false; // on the outer ring, one of the hot entry's variants
};

// What an open wheel shows, in window pixels, taken from the session's layout
// -- the numbers it hit-tests -- and the model's entries. Closed is empty.
struct RadialMenuFace
{
    bool Open = false;
    Vec2d Centre = {};
    float PlateRadius = 0.0f;
    float HubRadius = 0.0f;
    // The caption's line: under everything the wheel can show, as wide as it.
    float CaptionTop = 0.0f;
    float CaptionHeight = 0.0f;
    std::string Caption;
    bool CaptionNamesCurrent = false; // nothing is hot: it names what is already on
    std::vector<RadialMenuKey> Keys;
};

[[nodiscard]] RadialMenuFace FaceOf(const RadialMenuSession& wheel, const IRadialMenuModel& menu);
