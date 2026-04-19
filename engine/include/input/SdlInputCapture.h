#pragma once

#include <input/InputFrame.h>

union SDL_Event;

//=============================================================================
// SdlInputCapture
//
// Stateless translator from SDL_Event stream into InputFrame updates.
// The caller owns the InputFrame and pumps events through Accept() for each
// SDL_Event it already consumed. This keeps SDL out of simulation code.
//=============================================================================
class SdlInputCapture
{
public:
    // Start-of-frame: zero accumulated deltas, but retain held state and
    // unconsumed edges from the previous frame. Do NOT call ClearEdges()
    // here — that is the simulation's job on its first fixed tick.
    static void BeginFrame(InputFrame& frame);

    // Fold a single SDL_Event into the frame. Returns true if the event was
    // an input event that updated the frame; false if the event was unrelated.
    static bool Accept(InputFrame& frame, const SDL_Event& event);
};
