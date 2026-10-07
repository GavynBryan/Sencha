#pragma once

#include "documents/DocumentRef.h"
#include "workspaces/WorkspaceFacets.h"

#include <math/geometry/2d/Rect2d.h>

struct Problem;

class ActionContext;
struct FrameUpdateContext;
struct PlatformEventContext;
struct WorkspaceWindow;

// One kind of editing, alive only while open: what it is doing, which the shell
// presents through its facets, and the actions it performs while in use. It
// never draws the shell; its own content it shows on the window's surfaces
// (editor/ARCHITECTURE.md).
struct ProjectFile;

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
    // Where its content shows, in window pixels: the whole window but what the
    // host keeps for itself. Told every frame it is the one showing.
    virtual void SetContentRect(const Rect2d&) {}

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
    [[nodiscard]] virtual WorkspaceLocationFacet Location() const { return {}; }
    // Non-const: naming a resource's picture may start loading it.
    [[nodiscard]] virtual std::vector<WorkspaceResourceFacet> Resources() { return {}; }
    // Not const: the property sources it hands out are the workspace's to edit.
    [[nodiscard]] virtual WorkspaceSelectionFacet Selection() { return {}; }
    // Makes one of the open documents (WorkspaceDocumentFacet::Open) the one
    // being edited.
    virtual void ShowDocument(std::string_view /*key*/) {}
    // Opens a content file of a kind its WorkspaceKind::Opens names, or brings
    // it forward if it is open; false when it could not.
    virtual bool OpenAsset(const ProjectFile&) { return false; }
    // Shows where a problem in one of its documents is; false when it cannot.
    virtual bool RevealProblem(const Problem&) { return false; }
    // Not const: a clock reads what it is showing, which may settle it.
    [[nodiscard]] virtual WorkspaceTimelineFacet Timeline() { return {}; }
    // The deck's ruler dragged to `time`, in the clock's seconds.
    virtual void Scrub(double /*time*/, TimelineDrag) {}
    // A movable mark dragged to `time`: one edit from Begin to End.
    virtual void MoveMark(std::string_view /*key*/, double /*time*/, TimelineDrag) {}
    // A mark or a lane's span picked, by its key.
    virtual void PickOnTimeline(std::string_view /*key*/) {}
    [[nodiscard]] virtual std::vector<WorkspaceExplorerInfo> Explorers() const { return {}; }
    // Not const: picturing an item may start loading its image.
    [[nodiscard]] virtual ExplorerPage Explore(const ExplorerQuery&) { return {}; }
    // `mark` is the index of the mark chosen, for ExplorerChoice::Mark.
    virtual void ChooseInExplorer(std::string_view /*explorer*/, std::string_view /*key*/, ExplorerChoice,
                                  std::size_t /*mark*/)
    {
    }

};
