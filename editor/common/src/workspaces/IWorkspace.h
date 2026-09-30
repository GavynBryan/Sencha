#pragma once

#include "documents/DocumentRef.h"
#include "workspaces/WorkspaceFacets.h"

class ActionContext;
struct FrameUpdateContext;
struct PlatformEventContext;
struct WorkspaceView;
struct WorkspaceWindow;
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
    // Offered before the window's own UI sees the event: true claims it
    // outright, as a surface waiting for the next key to bind does.
    [[nodiscard]] virtual bool ClaimPlatformEvent(PlatformEventContext&) { return false; }
    // Platform events the window routes to its active workspace, after the
    // window's own UI has seen them.
    virtual void HandlePlatformEvent(PlatformEventContext&) {}
    // Whether a drag that began in this workspace's content has the pointer:
    // until it ends, nothing over the content takes the pointer from it.
    [[nodiscard]] virtual bool HoldsPointer() const { return false; }
    // The window the workspace now shows in: when it is first placed, and
    // whenever it moves to another.
    virtual void MoveTo(const WorkspaceWindow&) {}

    // Whether this workspace edits the document, and bringing it forward when
    // the journal is about to step it.
    [[nodiscard]] virtual bool OwnsDocument(const DocumentRef&) const { return false; }
    virtual void RevealDocument(const DocumentRef&) {}
    // Undoes an edit staged but not yet a step, such as a live preview, on its
    // own. False when there is none and the journal's newest step is next.
    virtual bool UndoStagedEdit() { return false; }
    [[nodiscard]] virtual bool HasStagedEdit() const { return false; }

    // The actions this workspace performs while it is the one in use. The host
    // puts them in play in the window showing it, and takes them out when it
    // hides or goes elsewhere; the definitions live in the ActionCatalog.
    [[nodiscard]] virtual ActionContext* Actions() { return nullptr; }

    // What the shell presents about this workspace (WorkspaceFacets.h).
    [[nodiscard]] virtual WorkspaceDocumentFacet Document() const { return {}; }
    [[nodiscard]] virtual WorkspaceTransportFacet Transport() const { return {}; }
    // Not const: the parameters it hands out are the workspace's to edit.
    [[nodiscard]] virtual WorkspaceOperationFacet Operation() { return {}; }
    [[nodiscard]] virtual WorkspaceManipulationFacet Manipulation() const { return {}; }
    [[nodiscard]] virtual WorkspaceViewFacet Viewing() const { return {}; }
    // Non-const: naming a resource's picture may start loading it.
    [[nodiscard]] virtual std::vector<WorkspaceResourceFacet> Resources() { return {}; }
    // Not const: the property sources it hands out are the workspace's to edit.
    [[nodiscard]] virtual WorkspaceSelectionFacet Selection() { return {}; }

    virtual WorkspaceView& View() = 0;
    // The window this workspace now draws in: its UI capture, input gates and
    // chrome surfaces.
    virtual void Place(EditorUiFeature&) {}
};
