#pragma once

#include <cstdint>

//=============================================================================
// SimulationAuthority
//
// Whether this process decides what happens in its World. A World resource
// with one writer -- the host that owns the session -- and many readers: any
// operation that mutates state other peers will see.
//
// True for a process with no session and for the host of one; false for a
// client, whose copy of the World is what the authority last said. Gameplay
// written against this once is correct in both: a standalone game is simply
// the authority of a session nobody else joined, which is what lets a
// single-player operation become a server-correct one by asking this before it
// writes, rather than by being rewritten when a session arrives.
//
// This is a fact, not a policy. It says nothing about ownership, prediction or
// relevance; an operation that needs those asks the net layer's own narrow
// dependencies. It exists so the common case -- "only the authority applies
// this" -- costs one read and no session handle.
//=============================================================================
struct SimulationAuthority
{
    bool Authoritative = true;
    // The authority's tick minus this process's, as a client estimates it;
    // zero on the authority. It settles a tick at a time, so a converted tick
    // can repeat or skip one.
    std::int64_t TickOffset = 0;
};

class World;

// The fact as this World holds it. A World with no resource is authoritative:
// that is the answer for every headless test and every process a session
// never touched.
[[nodiscard]] bool IsSimulationAuthority(const World& world);

// `localTick` as the authority numbers it: itself in a World with no resource
// or on the authority, and the estimate on a client. Floors at zero.
[[nodiscard]] std::uint64_t AuthorityTickOf(const World& world, std::uint64_t localTick);
