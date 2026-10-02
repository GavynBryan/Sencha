#include "authoring/AnimationBlendspacePad.h"

#include "authoring/AnimationPreviewSession.h"

#include <anim/AnimBlendspace.h>
#include <anim/AnimBlendspaceData.h>
#include <anim/AnimContentState.h>
#include <anim/AnimRigBinding.h>

#include <algorithm>
#include <array>

namespace
{
const AnimBoundBlendspace* PlayedOn(const AnimationPreviewSession& session, std::size_t layer)
{
    const AnimBoundRig* rig = session.Rig();
    const AnimContentState* content = session.Content();
    if (rig == nullptr || content == nullptr || layer >= rig->Layers.size() || layer >= kAnimMaxLayers)
        return nullptr;
    const std::uint16_t playing = content->Layers[layer].Content;
    if (playing >= rig->Contents.size() || rig->Contents[playing].Blendspace < 0)
        return nullptr;
    return &rig->Blendspaces[static_cast<std::size_t>(rig->Contents[playing].Blendspace)];
}

double Along(const AnimBoundBlendspaceAxis& axis, float value)
{
    return axis.Max > axis.Min ? std::clamp((value - axis.Min) / (axis.Max - axis.Min), 0.0f, 1.0f) : 0.5;
}

std::string LeafOf(const std::string& path)
{
    const std::size_t cut = path.find_last_of("/#:");
    return cut == std::string::npos ? path : path.substr(cut + 1);
}
} // namespace

std::optional<AnimationBlendspaceView> AnimationBlendspaceOn(const AnimationPreviewSession& session,
                                                             std::size_t layer)
{
    const AnimBoundBlendspace* space = PlayedOn(session, layer);
    if (space == nullptr)
        return std::nullopt;
    const AnimBoundRig& rig = *session.Rig();
    const AnimLayerContent& content = session.Content()->Layers[layer];
    std::array<float, kAnimBlendspaceMaxSamples> weights{};
    AnimBlendspaceWeights(*space, { content.Coordinates[0], content.Coordinates[1] }, weights);
    const bool plane = space->AxisCount > 1;

    AnimationBlendspaceView view{ .Path = space->Path, .Axes = plane ? 2u : 1u, .Phase = content.Phase };
    for (std::size_t a = 0; a < view.Axes; ++a)
        view.AxisFact[a] = space->Axes[a].Fact;
    view.Coordinates[0] = content.Coordinates[0];
    view.Coordinates[1] = content.Coordinates[1];
    view.U = Along(space->Axes[0], content.Coordinates[0]);
    view.V = plane ? Along(space->Axes[1], content.Coordinates[1]) : 0.5;
    for (std::size_t s = 0; s < space->Samples.size(); ++s)
    {
        const AnimBoundBlendspaceSample& sample = space->Samples[s];
        view.Samples.push_back({ .U = Along(space->Axes[0], sample.At[0]),
                                 .V = plane ? Along(space->Axes[1], sample.At[1]) : 0.5,
                                 .Weight = s < weights.size() ? weights[s] : 0.0f,
                                 .Label = sample.Content >= 0 ? LeafOf(rig.Contents[static_cast<std::size_t>(sample.Content)].Path)
                                                              : std::string("?") });
    }
    return view;
}

bool PlaceAnimationBlendspacePoint(AnimationPreviewSession& session, std::size_t layer, double u, double v)
{
    const AnimBoundBlendspace* space = PlayedOn(session, layer);
    if (space == nullptr)
        return false;
    const std::array<double, 2> at{ std::clamp(u, 0.0, 1.0), std::clamp(v, 0.0, 1.0) };
    for (std::size_t a = 0; a < space->AxisCount && a < 2; ++a)
    {
        const AnimBoundBlendspaceAxis& axis = space->Axes[a];
        AnimationScenarioValue value;
        value.Number = axis.Min + static_cast<float>(at[a]) * (axis.Max - axis.Min);
        session.SetFact(axis.Fact, value);
    }
    return true;
}
