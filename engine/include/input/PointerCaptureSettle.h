#pragma once

#include <cstdint>

//=============================================================================
// PointerCaptureSettle
//
// Whether this frame's pointer displacement describes something the player did.
//
// Toggling relative mouse mode teleports the cursor: entering it hides the
// pointer and takes over, leaving it puts the pointer back where the desktop
// thinks it is. The platform reports that jump the only way it reports any
// pointer movement, as relative motion -- so the frame around a capture change
// carries a displacement proportional to how far the cursor happened to be from
// where it is going, which is not input and must not turn a camera.
//
// Owned by whatever arbitrates capture, because that is the one thing that
// knows the pointer moved without the player moving it. Every cause is the same
// bug: resuming from a menu, alt-tabbing back, the console closing over a game,
// a viewport's hold-to-look beginning.
//
// Two frames, not one. The change can land either side of the platform pump --
// the shell applies it while resolving a transition, focus applies it during
// the pump itself -- and the motion it produces arrives on the pump after that.
// One frame covers whichever of those happened; the second covers the other.
//=============================================================================
class PointerCaptureSettle
{
public:
    // The applied capture state just changed.
    void NotifyChanged() { Frames = 2; }

    // Whether this frame's pointer displacement should be discarded. Does not
    // consume: a caller may ask before it decides what to read.
    [[nodiscard]] bool ShouldDropPointerMotion() const { return Frames > 0; }

    // Once per rendered frame, after the frame's events have been folded.
    void EndFrame()
    {
        if (Frames > 0)
            --Frames;
    }

    [[nodiscard]] std::uint8_t FramesRemaining() const { return Frames; }

private:
    std::uint8_t Frames = 0;
};
