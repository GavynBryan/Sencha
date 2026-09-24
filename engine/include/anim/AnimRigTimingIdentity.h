#pragma once

#include <cstdint>

struct AnimBoundRig;
class GameplayTagRegistry;

//=============================================================================
// AnimRigTimingIdentity
//
// What two machines must agree on about a rig for the same request set to play
// out the same way on both: the fact layout and its derivations, the request
// schema, which rule wins, what each behavior latches and how it cancels,
// which content a row plays and for how long, a flow's sections, loops and
// branches, and every gameplay-scope event. Named by tag, slot and intent
// names, never by this process's ids, so it means the same on every machine.
//
// Blends, weights, masks and cosmetic events are left out: changing them
// changes how a pose looks, not what happens, so a reload that touches only
// those is still compatible with a session in progress.
//=============================================================================
[[nodiscard]] std::uint64_t AnimRigTimingIdentity(const AnimBoundRig& rig, const GameplayTagRegistry* tags);
