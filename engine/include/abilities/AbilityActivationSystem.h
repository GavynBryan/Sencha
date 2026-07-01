#pragma once

struct FixedLogicContext;

//=============================================================================
// AbilityActivationSystem
//
// Drains the ability activation queue (input/AI intents -> effects/tags) once per
// fixed tick, for every active logic registry.
//=============================================================================
class AbilityActivationSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
};
