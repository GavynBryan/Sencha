#pragma once

#include "render/EditorLineSegment.h"

#include <math/geometry/3d/Aabb3d.h>

#include <cstdint>
#include <vector>

class AbilityPreviewSession;

struct AbilityStage
{
    std::vector<EditorLineSegment> Strokes;
    // What the stage's camera frames: where the pawn has been, and what it placed and struck.
    Aabb3d Bounds = Aabb3d::Empty();
};

// The preview as world-space lines up to the tick shown: the floor, the pawn's
// capsule where it stands, its trail, its aim, the targets placed so far, and
// what its activations' hits hold on that tick.
[[nodiscard]] AbilityStage AbilityStageOf(const AbilityPreviewSession& session, std::uint64_t shown);
