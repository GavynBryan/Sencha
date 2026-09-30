#pragma once

struct FrameUpdateContext;
struct PlatformEventContext;
struct WorkspaceView;
class EditorUiFeature;

// One kind of editing offered in a tab, alive only while open. The first half
// is toolkit-neutral; View and Place are what an ImGui window draws, and are
// the half a host with another toolkit replaces (editor/ARCHITECTURE.md).
class IWorkspace
{
public:
    virtual ~IWorkspace() = default;

    // Every frame while open, at FramePhase::Update, visible or not; a hidden
    // workspace decides what little it still has to do.
    virtual void Tick(FrameUpdateContext&) {}
    // Hiding is an interruption: a workspace sent to the background cancels
    // any edit in flight, as losing focus does.
    virtual void SetVisible(bool) {}
    // Platform events the window routes to its active workspace, after the
    // window's own UI has seen them.
    virtual void HandlePlatformEvent(PlatformEventContext&) {}

    virtual WorkspaceView& View() = 0;
    // The window this workspace now draws in: its UI capture, input gates and
    // chrome surfaces.
    virtual void Place(EditorUiFeature&) {}
};
