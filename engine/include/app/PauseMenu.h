#pragma once

#include <app/BackRouter.h>
#include <app/PauseMenuModel.h>
#include <app/PauseState.h>
#include <authored/VerbBindingSet.h>
#include <authored/VerbDispatcher.h>
#include <ui/UiScreenDesc.h>
#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class InputContextSet;
class RuntimeFrameLoop;
class UiService;

//=============================================================================
// PauseMenu
//
// The application shell's pages: which one is on top, what Back means there,
// and what the entries do.
//
// Owns its own stack, because the UI layer deliberately owns none --
// UiService::Navigate sends a key into a document and nothing more, and a modal
// flag traps focus. Routing is this layer's, and it is a vector rather than a
// router: pushing a page, popping one, and resuming when the last goes is the
// whole of it.
//
// Touches no SDL, no timescale and no input context. It asks PauseState for a
// transition and publishes to UiService; what a pause does to the world is the
// other object's answer.
//=============================================================================
class PauseMenu
{
public:
    PauseMenu(UiService& ui, UiSurfaceId surface, PauseState& pause, BackRouter& router);
    ~PauseMenu();

    PauseMenu(const PauseMenu&) = delete;
    PauseMenu& operator=(const PauseMenu&) = delete;
    PauseMenu(PauseMenu&&) = delete;
    PauseMenu& operator=(PauseMenu&&) = delete;

    [[nodiscard]] PauseMenuModel& Model() { return Model_; }
    [[nodiscard]] const PauseMenuModel& Model() const { return Model_; }

    // Opens the root page, pausing as it goes. What a HUD button calls.
    void Open();

    // Closes every page and resumes.
    void Close();

    [[nodiscard]] bool IsOpen() const { return !Pages.empty(); }
    [[nodiscard]] std::size_t Depth() const { return Pages.size(); }

    // A page on top of the current one: an options screen, a confirmation.
    //
    // Carries its own content and its own answer to being activated, so the
    // shell owns the stack and the drain while the page owns what it means. A
    // settings page's knowledge of settings stays inside it; the menu never
    // acquires any.
    //
    // Its model name must differ from every page already open -- a document
    // context holds one model per name, so two pages sharing one refuse to
    // open. Its first declared action is its activation, reported with the row
    // index the document repeated over.
    struct Page
    {
        UiScreenDesc Desc;
        // Called when the page opens, and again after anything it presents may
        // have changed.
        std::function<void(UiScreenHandle)> Publish;
        // Called with the row the document reported and the value the row's
        // control now shows, when the action carried one. The value travels
        // with the action rather than being read back from the model: the
        // document engine gives no order between writing the model and raising
        // the action, so a reread could see the value before the change.
        std::function<void(std::size_t, const UiValue&)> Activate;
        // Called once as the page goes, whether it was popped, closed with the
        // rest of the stack, or torn down with the shell. A page that changed
        // something durable commits it here rather than on every keystroke.
        std::function<void()> Closed;
    };

    void Push(Page page);

    // Drops the top page. Resuming when the last one goes is Back's job, not
    // this one's, so a handler can replace a page without leaving the shell.
    void Pop();

    // What the Resume entry calls. Deferred to the end of the frame's update
    // rather than acted on immediately: closing the page a handler is running
    // inside would destroy the model it was dispatched from.
    void RequestResume() { ResumeRequested = true; }

    // Marks the entries as changed, so the next update republishes them. A game
    // that renames or reorders an entry while the menu is open calls this.
    void MarkModelChanged() { ModelDirty = true; }

    // Where an entry's authored binding is resolved and what invokes it.
    //
    // Both null in a host that composed no vocabulary -- a tool, a test, a
    // dedicated server -- which is what makes an entry with an authored binding
    // report itself unavailable rather than reach through a null owner. Set
    // once during startup composition, before the menu can be opened.
    void SetVerbBindings(VerbDispatcher* dispatcher, const VerbBindingSet* bindings)
    {
        Verbs = dispatcher;
        VerbBindings = bindings;
    }

    // What the last drained activation resolved to, for a headless test and for
    // a diagnostic surface. Absent when the entry ran a native handler.
    [[nodiscard]] VerbAdmission LastAuthoredAdmission() const { return LastAdmission; }

    // Per frame, inside the host's frame update: act on what the documents
    // asked for, resolve any transition it caused, then reconcile what is open
    // against the state and publish. The engine updates the UI immediately
    // after, which is what makes an action taken this frame visible in it.
    void Update(RuntimeFrameLoop& runtime, InputContextSet& contexts);

    // The page stack's answer to Back. Registered with the router twice: as the
    // fallback that opens the shell from gameplay, and -- while pages are open
    // -- above game UI, so an inventory left open behind a menu cannot take the
    // player's Resume press.
    bool Back();

private:
    struct OpenPage
    {
        UiScreenHandle Screen;
        std::string ModelName;
        // Kept so a page can be reopened -- after a theme change, a surface
        // rebuild -- without the caller having to describe it again.
        UiScreenDesc Desc;
        std::function<void(UiScreenHandle)> Publish;
        std::function<void(std::size_t, const UiValue&)> Activate;
        std::function<void()> Closed;
    };

    [[nodiscard]] UiScreenDesc DescribeRoot() const;
    void OpenTop();
    void CloseTop();
    void PublishTop();
    void Dispatch(PauseCommandId command);

    UiService& Ui;
    UiSurfaceId Surface;
    PauseState& Pause;
    BackRouter& Router;
    PauseMenuModel Model_;

    std::vector<OpenPage> Pages;

    // The row-to-command relation the document is currently repeating over,
    // captured when the labels were published.
    //
    // A click is drained against what the player was looking at, not against
    // what the model says now: a game that reordered the menu between the
    // publish and the drain would otherwise make a queued press mean whichever
    // command moved into that row.
    std::vector<PauseCommandId> PublishedCommands;

    VerbDispatcher* Verbs = nullptr;
    const VerbBindingSet* VerbBindings = nullptr;
    VerbAdmission LastAdmission = VerbAdmission::Unavailable;

    // Held for the shell's lifetime: with nothing open, Back opens the menu.
    BackConsumerLease Fallback;
    // Held only while a page is open, in the band above game UI.
    BackConsumerLease Shell;

    // Set by a handler that asked to resume, applied once the dispatch that
    // asked has finished -- popping the page a handler is running inside would
    // destroy the model it was dispatched from.
    bool ResumeRequested = false;
    // Republished when the model changes shape rather than every frame, so a
    // menu that is merely open costs a comparison.
    bool ModelDirty = true;
};
