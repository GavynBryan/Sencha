#include "authoring/AnimationTimeline.h"

#include "authoring/AnimationPreviewWorkspace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace
{
constexpr std::array<double, 5> kSpeeds{ 0.25, 0.5, 1.0, 2.0, 4.0 };
}

bool AnimationTimeline::IsSimulation() const
{
    return Workspace.Viewport.Source == AnimationViewportSource::Simulation && Workspace.Rig.Simulation.IsOpen();
}

WorkspaceTimelineFacet AnimationTimeline::Facet() const
{
    if (IsSimulation())
    {
        const AnimationPreviewSession& simulation = Workspace.Rig.Simulation;
        const std::deque<AnimationPreviewTickRecord>& history = simulation.History();
        const double seconds = simulation.TickSeconds();
        const std::optional<std::size_t> inspected = Workspace.Rig.Navigation.InspectRecord;
        const AnimTick shown = inspected && *inspected < history.size() ? history[*inspected].Tick : simulation.Tick();
        return { .Clock = "Simulation",
                 .Start = history.empty() ? 0.0 : static_cast<double>(history.front().Tick) * seconds,
                 .End = static_cast<double>(simulation.Tick()) * seconds,
                 .Time = static_cast<double>(shown) * seconds,
                 .Playing = simulation.IsPlaying(),
                 .Live = !inspected.has_value(),
                 .Readout = std::format("tick {} / {} \xC2\xB7 {} Hz \xC2\xB7 {:g}\xC3\x97{}", shown, simulation.Tick(),
                                        simulation.Scenario().TickRate, simulation.Speed(),
                                        inspected ? " \xC2\xB7 recorded" : "") };
    }
    const AnimationClipPreviewSession& audition = Workspace.Audition.Session;
    if (audition.Skeleton().Joints.empty())
        return {};
    const double duration = audition.Duration();
    return { .Clock = "Audition",
             .End = duration,
             .Time = audition.NormalizedTime() * duration,
             .Playing = audition.IsPlaying(),
             .Live = !audition.IsInspecting(),
             .Readout = duration > 0.0 ? std::format("{:.2f} s / {:.2f} s \xC2\xB7 tick {} \xC2\xB7 {:g}\xC3\x97",
                                                     audition.NormalizedTime() * duration, duration, audition.Tick(),
                                                     audition.Speed())
                                       : std::string("Bind pose") };
}

void AnimationTimeline::TogglePlay()
{
    if (IsSimulation())
    {
        AnimationPreviewSession& simulation = Workspace.Rig.Simulation;
        if (simulation.IsPlaying())
            simulation.Pause();
        else
            simulation.Play();
        return;
    }
    AnimationClipPreviewSession& audition = Workspace.Audition.Session;
    if (audition.IsInspecting())
        audition.ReturnToPlayback();
    if (audition.IsPlaying())
        audition.Pause();
    else
        audition.Play();
}

bool AnimationTimeline::IsPlaying() const
{
    return IsSimulation() ? Workspace.Rig.Simulation.IsPlaying() : Workspace.Audition.Session.IsPlaying();
}

void AnimationTimeline::Restart()
{
    if (!IsSimulation())
    {
        Workspace.Audition.Session.Restart();
        return;
    }
    Workspace.Rig.Navigation.InspectRecord.reset();
    Workspace.Rig.Simulation.Restart();
}

void AnimationTimeline::Step(int direction)
{
    if (!IsSimulation())
    {
        Workspace.Audition.Session.Step(direction);
        return;
    }
    AnimationNavigation& navigation = Workspace.Rig.Navigation;
    const std::size_t recorded = Workspace.Rig.Simulation.History().size();
    if (direction > 0)
    {
        if (!navigation.InspectRecord)
            Workspace.Rig.Simulation.Step();
        else if (*navigation.InspectRecord + 1 < recorded)
            ++*navigation.InspectRecord;
        else
            navigation.InspectRecord.reset();
        return;
    }
    if (recorded < 2)
        return;
    const std::size_t at = navigation.InspectRecord.value_or(recorded - 1);
    navigation.InspectRecord = at > 0 ? at - 1 : 0;
}

void AnimationTimeline::ChangeSpeed(int direction)
{
    const double current = IsSimulation() ? Workspace.Rig.Simulation.Speed() : Workspace.Audition.Session.Speed();
    std::size_t index = 0;
    while (index + 1 < kSpeeds.size() && kSpeeds[index] < current)
        ++index;
    const std::size_t next = direction > 0 ? std::min(index + 1, kSpeeds.size() - 1) : (index > 0 ? index - 1 : 0);
    // Both clocks keep one speed, so switching between them does not change it.
    (void)Workspace.Rig.Simulation.SetSpeed(kSpeeds[next]);
    (void)Workspace.Audition.Session.SetSpeed(kSpeeds[next]);
}

void AnimationTimeline::Scrub(double seconds)
{
    if (!IsSimulation())
    {
        AnimationClipPreviewSession& audition = Workspace.Audition.Session;
        if (const double duration = audition.Duration(); duration > 0.0)
            audition.InspectNormalized(std::clamp(seconds / duration, 0.0, 1.0));
        return;
    }
    const AnimationPreviewSession& simulation = Workspace.Rig.Simulation;
    const std::deque<AnimationPreviewTickRecord>& history = simulation.History();
    if (history.empty())
        return;
    const double tick = seconds / simulation.TickSeconds();
    std::size_t nearest = 0;
    for (std::size_t i = 1; i < history.size(); ++i)
        if (std::abs(static_cast<double>(history[i].Tick) - tick)
            < std::abs(static_cast<double>(history[nearest].Tick) - tick))
            nearest = i;
    if (nearest + 1 == history.size())
        Workspace.Rig.Navigation.InspectRecord.reset();
    else
        Workspace.Rig.Navigation.InspectRecord = nearest;
}

void AnimationTimeline::ReturnToLive()
{
    if (IsSimulation())
        Workspace.Rig.Navigation.InspectRecord.reset();
    else
        Workspace.Audition.Session.ReturnToPlayback();
}

bool AnimationTimeline::IsLive() const
{
    return IsSimulation() ? !Workspace.Rig.Navigation.InspectRecord.has_value()
                          : !Workspace.Audition.Session.IsInspecting();
}

void AnimationTimeline::RunAhead()
{
    if (!IsSimulation())
        return;
    AnimationPreviewSession& simulation = Workspace.Rig.Simulation;
    Workspace.Rig.Navigation.InspectRecord.reset();
    simulation.RunTo(simulation.Tick() + simulation.Scenario().TickRate);
}
