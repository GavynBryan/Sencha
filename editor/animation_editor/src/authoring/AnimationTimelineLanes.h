#pragma once

#include "authoring/AnimationLens.h"

#include "workspaces/facets/TimelineFacet.h"

#include <anim/AnimTypes.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

class AnimationClipEventsDocument;
class AnimationPreviewSession;
class AnimationSessionLab;

// What a lane's span names when picked: a layer, and the rule that won it,
// the flow section it played, the tick a blend began, a request's sequence,
// or a lab run's tick; -1 for none. A span of a lens's lane names that lens.
struct AnimationLanePick
{
    enum class What : std::uint8_t
    {
        Rule,
        Section,
        Blend,
        Request,
        LabTick,
    };
    What Names = What::Rule;
    std::size_t Layer = 0;
    int Index = -1;
    std::optional<AnimationLens> Lens;
};

// What won `layer` along the simulation's recorded ticks: a span per stretch
// one rule held, named for the rule, or for the behavior on a layer chosen by
// requests. The span of rule `picked` is selected.
[[nodiscard]] TimelineLane AnimationDecisionLane(const AnimationPreviewSession& session, std::size_t layer,
                                                 int picked);
// The flow sections `layer` played along the recorded ticks: a span per pass
// through a section, none while it plays no flow.
[[nodiscard]] TimelineLane AnimationFlowLane(const AnimationPreviewSession& session, std::size_t layer);
// The blends applied on `layer` along the recorded ticks, each a span as long
// as it ran.
[[nodiscard]] TimelineLane AnimationBlendLane(const AnimationPreviewSession& session, std::size_t layer);
// How far root motion carried the character on each recorded tick, and how
// far it got: two plotted lanes, none while it poses in place.
[[nodiscard]] std::vector<TimelineLane> AnimationTravelLanes(const AnimationPreviewSession& session);
// Fact `fact` on each recorded tick, plotted; empty when the rig has no such fact.
[[nodiscard]] TimelineLane AnimationFactLane(const AnimationPreviewSession& session, std::string_view fact);
// Each request issued along the recorded ticks, a span from its start for as
// long as it was live; the index a span names is the request's sequence.
[[nodiscard]] TimelineLane AnimationRequestLane(const AnimationPreviewSession& session);
// A session lab run along its own clock: where the client agreed with the
// authority, span by span (each naming its first tick), with the stretch
// holding `inspected` selected; and the client's pose residual, plotted.
[[nodiscard]] std::vector<TimelineLane> AnimationLabLanes(const AnimationSessionLab& lab, AnimTick inspected);
[[nodiscard]] std::optional<AnimationLanePick> ParseAnimationLaneKey(std::string_view key);

// The clip's events as marks along a clip `duration` long, `picked` selected:
// a diamond for gameplay, a circle for cosmetic, each dragged to retime it.
[[nodiscard]] std::vector<TimelineMark> AnimationEventMarks(const AnimationClipEventsDocument& events,
                                                            double duration, std::optional<std::uint32_t> picked);
// The event a mark's key names.
[[nodiscard]] std::optional<std::uint32_t> ParseAnimationEventMarkKey(std::string_view key);
