#pragma once

#include "render/EditorLineSegment.h"

#include <anim/AnimTypes.h>
#include <math/Mat.h>

#include <optional>
#include <span>
#include <vector>

class AnimationPreviewSession;
struct SkeletonData;

// The world-space strokes drawn over the rig on its stage, for the line
// pipeline: what a picture of the pose alone cannot show.

// A bone from each joint to its parent, the posed `model` transforms giving
// where each joint is.
void AppendSkeletonStrokes(const SkeletonData& skeleton, std::span<const Mat4> model,
                           std::vector<EditorLineSegment>& out);

// Where a simulated character went, up to the tick shown: its trail at its
// feet, each move root motion asked for and a wall stopped, and the scenario's
// walls.
void AppendPathStrokes(const AnimationPreviewSession& session, std::optional<AnimTick> shown,
                       std::vector<EditorLineSegment>& out);
