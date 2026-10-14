#pragma once

#include "documents/DocumentRef.h"
#include "workspaces/facets/DocumentFacet.h"
#include "workspaces/facets/LocationFacet.h"
#include "workspaces/facets/ManipulationFacet.h"
#include "workspaces/facets/OperationFacet.h"
#include "workspaces/facets/ResourceFacet.h"
#include "workspaces/facets/SelectionFacet.h"
#include "workspaces/facets/TransportFacet.h"
#include "workspaces/facets/ViewFacet.h"

#include <math/Vec.h>
#include <math/geometry/2d/Rect2d.h>

struct Problem;

class ActionContext;
class ExplorerTable;
class IWorkspaceTimeline;
struct FrameUpdateContext;
struct PlatformEventContext;
struct WorkspaceWindow;

struct ProjectFile;

// One kind of editing, alive only while open: what it is doing, which the shell
// presents through its facets (plain facts: a shell decides whether a document
// is a dial or a tab), and the actions it performs while in use. It never draws
// the shell; its own content it shows on the window's surfaces
// (editor/ARCHITECTURE.md).
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

    // What the shell presents about this workspace (workspaces/facets/).
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
    // A content file carried from the shelf and let go over the content at
    // `point`, in window pixels: what that means is the workspace's -- a prefab
    // placed there, a material put on the face there. False when it means nothing.
    virtual bool DropAsset(const ProjectFile&, Vec2d /*point*/) { return false; }
    // A content file carried off the shelf over the content: whether this
    // workspace takes the rest of that gesture, and taking it at `point` once
    // the shell has handed the gesture's events, release included, over.
    [[nodiscard]] virtual bool TakesCarry(const ProjectFile&) const { return false; }
    virtual void TakeCarry(const ProjectFile&, Vec2d /*point*/) {}
    // Shows where a problem in one of its documents is; false when it cannot.
    virtual bool RevealProblem(const Problem&) { return false; }
    // Its work over time, for a deck; null when it keeps no time.
    [[nodiscard]] virtual IWorkspaceTimeline* Timeline() { return nullptr; }
    // What it lets a person browse and pick from, by id; null when nothing.
    [[nodiscard]] virtual ExplorerTable* Explorers() { return nullptr; }
};
