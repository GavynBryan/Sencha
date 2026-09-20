#pragma once

#include <authored/VerbInvocation.h>

#include <cstddef>
#include <vector>

//=============================================================================
// VerbTraceRing
//
// A bounded history of what was asked and what happened, for answering "why did
// that fire" after the fact.
//
// Compact ids only. A record outlives the binding asset that produced it, the
// module that declared the verb, and the entity that asked, so borrowing a
// string from any of them would leave the history holding pointers into freed
// content. An inspector resolves labels when it reads, against whatever is
// still live, and says so when a name has gone.
//
// Optional by construction: a host with no ring allocates nothing and still
// propagates invocation identity, because causality is part of the contract and
// tracing is not.
//=============================================================================

enum class VerbTraceEvent : std::uint8_t
{
    // The dispatcher offered the request to an implementation, which took it.
    Admitted,
    // The request never reached an implementation, or the implementation
    // declined it. Status says which.
    Rejected,
    // A deferred operation ran a request it had previously admitted.
    Executed,
    // A deferred operation could not run one: the target had gone, authority
    // had moved, or the operation gave up on it.
    Abandoned,
};

struct VerbTraceRecord
{
    VerbTraceEvent Event = VerbTraceEvent::Admitted;
    VerbAdmission Status = VerbAdmission::Accepted;
    VerbId Verb;
    InvocationId Id;
    InvocationId Parent;
    VerbBindingKey Binding;
    EntityId Producer;
};

class VerbTraceRing
{
public:
    explicit VerbTraceRing(std::size_t capacity);

    void Record(const VerbTraceRecord& record);

    // Oldest first, which is the order a causality chain reads in.
    [[nodiscard]] std::vector<VerbTraceRecord> Snapshot() const;

    [[nodiscard]] std::size_t Capacity() const { return Entries.size(); }

    // Records overwritten because the ring wrapped. A history that quietly lost
    // its beginning is one that explains the wrong thing.
    [[nodiscard]] std::size_t DroppedCount() const { return Dropped; }

    void Clear();

private:
    std::vector<VerbTraceRecord> Entries;
    std::size_t Next = 0;
    std::size_t Live = 0;
    std::size_t Dropped = 0;
};
