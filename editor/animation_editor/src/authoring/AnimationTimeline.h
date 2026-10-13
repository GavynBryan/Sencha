#pragma once

#include "workspaces/facets/TimelineFacet.h"

#include <anim/AnimTypes.h>

class AnimationPreviewWorkspace;

// The one clock the deck shows and drives (docs/editor/animation-workspace.md,
// The tape deck): the session lab's while its sheet is up and it has run, the
// simulation's once a rig runs and the stage shows it, the audition's otherwise.
class AnimationTimeline
{
public:
    explicit AnimationTimeline(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    // Whether the lab's sheet is up.
    void ShowLab(bool shown) { LabShown = shown; }
    // The lab's clock is only looked through: nothing plays or steps it.
    [[nodiscard]] bool IsLab() const;
    [[nodiscard]] bool IsSimulation() const;
    [[nodiscard]] WorkspaceTimelineFacet Facet() const;

    void TogglePlay();
    [[nodiscard]] bool IsPlaying() const;
    void Restart();
    void Step(int direction);
    void ChangeSpeed(int direction);
    void Scrub(double seconds);
    void InspectLabTick(AnimTick tick);
    void ReturnToLive();
    [[nodiscard]] bool IsLive() const;
    // The simulation a second further on.
    void RunAhead();

private:
    [[nodiscard]] WorkspaceTimelineFacet LabFacet() const;

    AnimationPreviewWorkspace& Workspace;
    bool LabShown = false;
};
