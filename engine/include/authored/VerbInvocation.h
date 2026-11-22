#pragma once

#include <authored/AuthoredClassification.h>
#include <authored/AuthoredValue.h>
#include <authored/VerbId.h>
#include <ecs/EntityId.h>
#include <ecs/LeaseOwner.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

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
    // A cosmetic producer named a gameplay verb.
    ScopeMismatch,
    // A lease was invoked as an instant verb, or an instant verb acquired as a lease.
    KindMismatch,
};

// Which half of a lease a call is; None for an instant verb.
enum class LeaseStep : std::uint8_t
{
    None,
    Acquire,
    Release,
    // Accepted while the owner's hold goes on; sent only to a lease that Completes.
    Probe,
};

[[nodiscard]] const char* VerbAdmissionName(VerbAdmission admission);

// What an operation answers: an admission, and a detail that names why.
struct VerbAnswer
{
    VerbAdmission Status = VerbAdmission::Accepted;
    // Refused: a reason code the verb declares; InvalidArguments, UnresolvedReference: one past the slot at fault.
    std::uint16_t Detail = 0;

    constexpr VerbAnswer() = default;
    constexpr VerbAnswer(VerbAdmission status, std::uint16_t detail = 0)
        : Status(status)
        , Detail(detail)
    {
    }

    [[nodiscard]] constexpr bool Accepted() const { return Status == VerbAdmission::Accepted; }
};

// Accepted for a reason enum's zero value, otherwise Refused naming it.
template<typename E>
    requires std::is_enum_v<E>
[[nodiscard]] constexpr VerbAnswer VerbAnswerOf(E reason)
{
    return reason == E{} ? VerbAnswer{} : VerbAnswer(VerbAdmission::Refused, static_cast<std::uint16_t>(reason));
}

// InvalidArguments or UnresolvedReference naming the argument at `slot`.
[[nodiscard]] constexpr VerbAnswer VerbArgumentAnswer(VerbAdmission status, std::size_t slot)
{
    return VerbAnswer(status, static_cast<std::uint16_t>(slot + 1));
}

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

    // Diagnostic attribution: which authored record, and which entity the
    // request came through -- a relay, a screen's owner. An operation's target
    // is a typed argument, never this.
    VerbBindingKey Binding;
    EntityId Producer;

    // Who caused it: the participant entity behind the request, when a producer
    // knows one -- the local player for a menu or a console command, the peer's
    // participant for a request that arrived over a session, the owner of an
    // ability. Provenance for an operation that has to decide whether the
    // request is allowed; never a target, and never authority by itself. An
    // operation on the authority still validates it against what the World
    // says that participant may do.
    EntityId Instigator;

    // The fixed tick the request was produced in, or zero from a producer on
    // the frame clock. What an operation that runs a predicted tick twice keys
    // its "already applied" on, so a replayed tick does not admit twice.
    std::uint64_t Tick = 0;

    // A lease's holder, and whether this call acquires or releases it. A release
    // carries no arguments: the implementation ends whatever the owner holds.
    LeaseOwner Owner;
    LeaseStep Step = LeaseStep::None;

    // Borrowed for exactly the duration of this call. An implementation that
    // defers copies what it needs, or keeps its own owning copy; it never
    // stores this pointer.
    const AuthoredArguments* Arguments = nullptr;

    // Where a verb that declares a result writes it; borrowed for the call, null when nobody asked.
    AuthoredValue* Result = nullptr;
};

struct VerbInvocationResult
{
    VerbAdmission Status = VerbAdmission::Unavailable;
    InvocationId Id;
    std::uint16_t Detail = 0;

    [[nodiscard]] bool Accepted() const { return Status == VerbAdmission::Accepted; }
};

[[nodiscard]] constexpr VerbAnswer AnswerOf(const VerbInvocationResult& result)
{
    return VerbAnswer(result.Status, result.Detail);
}

// Optional attribution a producer supplies. Separate from the invocation so a
// producer with nothing to say passes nothing.
//
// On the wire, none of this and none of AuthoredArguments travels. The stable form
// of a request is the binding's authored key plus its producer inputs in their
// authored kinds -- persistent identities, names, paths -- and a producer that
// receives one over a session resolves wire identities to handles at its own
// ingress, before Invoke. What crosses this boundary is already local.
struct VerbInvocationSource
{
    InvocationId Parent{};
    EntityId Producer{};
    EntityId Instigator{};
    std::uint64_t Tick = 0;
    AuthoredScope Scope = AuthoredScope::Gameplay;
};
