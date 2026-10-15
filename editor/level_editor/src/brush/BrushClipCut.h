#pragma once

#include "brush/BrushMesh.h"
#include "brush/BrushOps.h"

#include <math/geometry/3d/Plane.h>

#include <cstdint>
#include <string>
#include <vector>

// A brush cut by a plane, judged once for the clip tool's preview and commit
// and for the clip operation alike.

enum class ClipMode : std::uint8_t
{
    KeepFront, // the side the plane's normal points to stays
    KeepBack,
    Split, // both stay, as two brushes
};

struct ClipCut
{
    enum class Result : std::uint8_t
    {
        Missed,  // the plane does not cross the brush; untouched
        Valid,   // both halves validate
        Invalid, // a half did not validate
    };

    Result     Outcome = Result::Missed;
    BrushMesh  Front;
    BrushMesh  Back;
    // The stage that refused, when Invalid: "front kernel refused", "back open".
    std::string Why;
};

// `plane` is in the mesh's own space. A capped cut of a closed solid must come
// out as two closed solids; an open cut only has to leave usable meshes.
[[nodiscard]] ClipCut CutBrush(const BrushMesh& mesh, const Plane& plane, BrushOps::ClipCap cap);

// What a valid cut keeps under `mode`, one solid per mesh: the brush keeps the
// first, and every other becomes a brush of its own. A kept half that came
// apart is several.
[[nodiscard]] std::vector<BrushMesh> KeptShells(const ClipCut& cut, ClipMode mode);
