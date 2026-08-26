#pragma once

#include <app/GameContexts.h>

//=============================================================================
// AimFacingSystem
//
// Turns the aim of every AimFacing entity into the rotation of its body.
//
// This is the second reader of LookOrientation, beside the camera. Both take
// the same accumulated aim and present it: one places a view, this one places a
// body. They are deliberately unaware of each other, which is what lets a third
// person camera orbit an entity that faces somewhere else -- that entity simply
// does not carry the tag, and nothing has to be unwound to arrange it.
//
// Yaw only. Pitch belongs to whatever presents the aim in the vertical: a lens,
// or an animated spine. A body pitched by its look would lie on its back
// looking up.
//
// Nothing here is about the network. On an authority the rotation it writes is
// what replicates out; on the machine driving the entity it is what makes
// aiming feel immediate; on a machine merely watching, replication interpolates
// the authority's rotation over the top afterwards. The ordering that makes the
// last of those true is declared where the net systems are registered.
//=============================================================================
class AimFacingSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
};
