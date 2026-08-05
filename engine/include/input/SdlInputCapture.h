#pragma once

#include <input/InputControl.h>
#include <input/InputFrame.h>

#include <optional>

union SDL_Event;

//=============================================================================
// SdlInputCapture
//
// Stateless translator from SDL_Event stream into InputFrame updates.
// The caller owns the InputFrame and pumps events through Accept() for each
// SDL_Event it already consumed. This keeps SDL out of simulation code.
//
// Mouse deltas are passed through as raw pixel displacement. Game code is
// responsible for sensitivity scaling. Do NOT normalize by dt — SDL delivers
// pixels per-event and per-frame accumulation is already frame-rate agnostic.
//=============================================================================
class SdlInputCapture
{
public:
    // Start-of-frame: zero accumulated deltas, but retain held state and
    // unconsumed edges from the previous frame. Do NOT call ClearEdges() here —
    // that is the input mapper's job, once per frame before any fixed tick.
    static void BeginFrame(InputFrame& frame);

    // Fold a single SDL_Event into the frame. Returns true if the event was
    // an input event that updated the frame; false if the event was unrelated.
    static bool Accept(InputFrame& frame, const SDL_Event& event);
};

// The control a raw press names, for flows that ask the player to touch a
// control rather than name one: a rebinding screen, an authoring tool's
// listen-for-input field.
//
// A stick or trigger only names itself past `axisThreshold` of full deflection,
// so resting drift and a brushed stick do not bind. Pointer motion deliberately
// names nothing: it moves while the player reaches for the control they meant,
// and would claim every capture. Events naming no control return nothing.
[[nodiscard]] std::optional<InputControl> InputControlFromSdlEvent(const SDL_Event& event,
                                                                  float axisThreshold);
