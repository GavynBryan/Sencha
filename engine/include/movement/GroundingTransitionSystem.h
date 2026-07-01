#pragma once

struct FixedLogicContext;
class World;

//=============================================================================
// Grounding transition: the built-in ground/air eligibility. Reads
// CharacterController.Grounded and requests OnGround or InAir at the base
// priority; the mode arbiter applies the winner. A game adds a higher-priority
// mode (slide, swim) without editing this.
//=============================================================================

void RequestGroundingLocomotionModes(World& world);

class GroundingTransitionSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
    void Step(World& world);
};
