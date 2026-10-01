#pragma once

#include "workspaces/WorkspaceFacets.h"

class AnimationPreviewWorkspace;

//=============================================================================
// AnimationTimeline
//
// The one clock the deck shows and drives: the simulation's once a rig runs
// and the stage shows it, the audition's otherwise. The audition scrubs by
// inspecting a moment of its clip; the simulation runs only forward, so
// scrubbing and stepping back walk its recorded history, and Live returns to
// the tick it is running.
//=============================================================================
class AnimationTimeline
{
public:
    explicit AnimationTimeline(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] bool IsSimulation() const;
    [[nodiscard]] WorkspaceTimelineFacet Facet() const;

    void TogglePlay();
    [[nodiscard]] bool IsPlaying() const;
    void Restart();
    void Step(int direction);
    void ChangeSpeed(int direction);
    void Scrub(double seconds);
    void ReturnToLive();
    [[nodiscard]] bool IsLive() const;
    // The simulation a second further on.
    void RunAhead();

private:
    AnimationPreviewWorkspace& Workspace;
};
