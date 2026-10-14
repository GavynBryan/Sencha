#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

class AnimationPreviewSession;

// One sample of a blendspace as a pad shows it: where it sits over the axes'
// ranges, 0 to 1 with the second axis up, and how much it weighs at the point.
struct AnimationBlendspaceMark
{
    double U = 0.0;
    double V = 0.0;
    float Weight = 0.0f;
    std::string Label = {};
};

// The blendspace a layer plays: its axes' facts (one, or two), its samples,
// and the point it is at, placed the same way.
struct AnimationBlendspaceView
{
    std::string Path;
    std::string AxisFact[2] = {};
    std::size_t Axes = 1;
    std::vector<AnimationBlendspaceMark> Samples = {};
    double U = 0.0;
    double V = 0.5;
    float Coordinates[2] = {};
    float Phase = 0.0f;
};

// Nothing when the layer plays no blendspace.
[[nodiscard]] std::optional<AnimationBlendspaceView> AnimationBlendspaceOn(const AnimationPreviewSession& session,
                                                                         std::size_t layer);
// Sets the axes' facts to the point at `u`, `v` for the next tick: a live fact
// the scenario records, never an asset change.
bool PlaceAnimationBlendspacePoint(AnimationPreviewSession& session, std::size_t layer, double u, double v);
