#pragma once

#include <authored/VerbArguments.h>
#include <authored/VerbId.h>
#include <ecs/EntityId.h>

#include <cstdint>

//=============================================================================
// What an operation is handed, and what it answers
//
// An implementation includes this and nothing else from the authored layer. It
// receives its own dependencies when it is composed; the invocation carries no
// Engine, no service pointer, no resource lookup, and no way to reach the
// dispatcher that called it.
//=============================================================================

// The answer to "did you take responsibility for this request".
//
// Accepted does not mean the gameplay outcome happened. An operation that
// queues work accepts now and acts at its own drain, and a target that has gone
// by then is that operation's business to report, not a reason to have refused
// admission.
enum class VerbAdmission : std::uint8_t
{
    Accepted,

    // The binding never resolved, or resolved against a different catalog.
    UnresolvedBinding,
    // It resolved against this catalog, but the verb has since been retired or
    // its argument contract has moved. Recompile before reusing it.
    StaleBinding,
    // The verb is declared but nothing is bound behind it, or the host has
    // closed admission for shutdown. Discovery and execution are separate
    // facts, and an editor holds the first without the second.
    Unavailable,
    // The producer's values do not match what the binding's inputs declared.
    InvalidArguments,
    // The implementation declined this particular request.
    Refused,
    // The implementation's queue is full. Visible refusal beats dropping a
    // request that was already reported as accepted.
    QueueFull,
    // Dispatch was entered from inside a dispatch. Refused whole: a partially
    // executed recursive chain is worse than a diagnostic.
    Reentrant,
    // An entity the binding names by persistent identity is not in this World
    // right now. A one-shot request against an absent target is refused, not
    // held for a later incarnation.
    UnresolvedReference,
};

[[nodiscard]] const char* VerbAdmissionName(VerbAdmission admission);

// One request, as the implementation sees it.
struct VerbInvocation
{
    VerbId Verb;

    // Minted before the implementation is called, so an operation that queues
    // work can put it in the record it queues. Reported to the producer only if
    // the implementation accepts; a refusal leaves a gap in the sequence, which
    // is cheaper than a second round trip.
    InvocationId Id;

    // Who asked, when something did. Causality for tracing; never an authority
    // claim and never a gameplay input.
    InvocationId Parent;

    // Diagnostic attribution: which authored record, and which entity produced
    // it. An operation's target is a typed argument, never this.
    VerbBindingKey Binding;
    EntityId Producer;

    // Borrowed for exactly the duration of this call. An implementation that
    // defers copies what it needs, or keeps its own owning copy; it never
    // stores this pointer.
    const VerbArguments* Arguments = nullptr;
};

struct VerbInvocationResult
{
    VerbAdmission Status = VerbAdmission::Unavailable;
    InvocationId Id;

    [[nodiscard]] bool Accepted() const { return Status == VerbAdmission::Accepted; }
};

// Optional attribution a producer supplies. Separate from the invocation so a
// producer with nothing to say passes nothing.
struct VerbInvocationSource
{
    InvocationId Parent;
    EntityId Producer;
};
