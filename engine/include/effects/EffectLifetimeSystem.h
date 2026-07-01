#pragma once

struct FixedLogicContext;

//=============================================================================
// EffectLifetimeSystem
//
// Ages and expires active effects once per fixed tick over the active logic
// registries. Runs after attribute resolve (and, in the movement pipeline, after
// locomotion) so a one-tick request tag lives for the tick that consumes it.
//=============================================================================
class EffectLifetimeSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
};
