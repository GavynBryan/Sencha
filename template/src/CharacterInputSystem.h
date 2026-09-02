#pragma once

struct FixedLogicContext;

// Turns this tick's resolved actions into movement intent, for every entity the
// movement vocabulary marks as controlled -- the player sitting at this
// machine and every peer whose commands arrived, each steering from its own
// input source and along its own aim.
struct CharacterInputSystem
{
    void FixedLogic(FixedLogicContext& ctx);
};
