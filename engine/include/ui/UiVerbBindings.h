#pragma once

#include <authored/VerbBindingSet.h>
#include <authored/VerbDispatcher.h>
#include <ui/UiAction.h>
#include <ui/UiScreenDesc.h>
#include <ui/UiScreenHandle.h>
#include <ui/UiValue.h>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

//=============================================================================
// UiVerbBindings
//
// What turns one screen's actions into authored invocations.
//
// It depends on the public UI values and on the shared authored mechanism, and
// on nothing a game implements. The direction is deliberate: UI integration may
// know about verbs, and the authored layer may not know about UI.
//
// It does not drain. Exactly one host drains a screen -- a second consumer
// racing for the batch would take actions the first never sees -- so the host
// drains once and hands the copied batch to whoever needs it, this included.
//
// The association is with one open screen handle and the exact description it
// was opened with. Reopening, reordering the declared actions, or closing the
// screen invalidates it, because a screen-local action id is a position in a
// list that only that opening declared.
//=============================================================================

// How one presentation argument becomes one of the binding's declared inputs.
//
// Both halves are explicit. A document's data expression has whatever type RML
// gave it, and guessing that a number was meant as an entity, or that an
// identity was meant as an index, is how a wrong click becomes a wrong action
// rather than a refused one.
struct UiVerbArgumentMapping
{
    // Position in UiAction::Arguments.
    std::size_t ArgumentIndex = 0;

    // Which of the binding's declared inputs this fills, by position.
    std::size_t InputSlot = 0;

    // What the document is expected to send. A payload of another kind is
    // refused rather than converted.
    UiValueKind Expected = UiValueKind::None;

    // What it becomes. The conversion is compiled when the screen opens and
    // checked against what the binding's input declared, so a mismatch is a
    // diagnostic at open rather than a refusal on every click.
    VerbValueKind Produces = VerbValueKind::None;
};

// One of a screen's declared actions, and what it invokes.
struct UiVerbActionMapping
{
    // As the document raises it, and as UiScreenDesc::Actions declares it.
    std::string ActionName;

    // Which binding in the set. The authored key, not a verb name: what a
    // button does is a binding, and which verb that binding names is the
    // binding's business.
    std::string BindingKey;

    std::vector<UiVerbArgumentMapping> Arguments;
};

class UiVerbBindings
{
public:
    UiVerbBindings(VerbDispatcher& dispatcher, const VerbBindingSet& bindings);

    // Compiles the mappings against one opening of one screen. False leaves the
    // controller closed and appends a diagnostic per problem: an action the
    // screen does not declare, a binding the set does not hold, an input slot
    // the binding does not have, or a conversion neither side agrees on.
    [[nodiscard]] bool Open(UiScreenHandle screen,
                            const UiScreenDesc& desc,
                            std::span<const UiVerbActionMapping> mappings,
                            std::vector<std::string>& errors);

    void Close();
    [[nodiscard]] bool IsOpen() const { return Screen.IsValid(); }

    // Invokes whatever in the batch belongs to this screen and this controller.
    // Actions for other screens, and actions with no mapping, are left alone --
    // a host routes one batch to several consumers.
    void Dispatch(std::span<const UiAction> actions);

    // What the last Dispatch decided, for a headless test and for a diagnostic
    // surface. Cleared at the start of each batch.
    struct Outcome
    {
        UiActionId Action;
        VerbAdmission Status = VerbAdmission::Unavailable;
        InvocationId Id;
    };
    [[nodiscard]] std::span<const Outcome> LastOutcomes() const { return Outcomes; }

private:
    struct CompiledAction
    {
        UiActionId Action;
        const CompiledVerbBinding* Binding = nullptr;
        std::vector<UiVerbArgumentMapping> Arguments;
        std::size_t InputCount = 0;
    };

    [[nodiscard]] const CompiledAction* Find(UiActionId action) const;

    VerbDispatcher& Dispatcher;
    const VerbBindingSet& Bindings;

    UiScreenHandle Screen;
    std::vector<CompiledAction> Actions;

    // Reused across batches so a click costs no allocation once the widest
    // mapping has been seen.
    std::vector<VerbValue> Inputs;
    std::vector<Outcome> Outcomes;
};
