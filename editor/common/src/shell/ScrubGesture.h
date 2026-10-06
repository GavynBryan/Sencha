#pragma once

#include "properties/PropertyNode.h"

// How a number dragged across its box moves: pixels to an amount, a tenth as
// fast with Shift, in coarse steps with Ctrl. A press becomes a drag only once
// the pointer has gone a few pixels; before that it is still a click, which
// types into the box instead.
struct ScrubScale
{
    double PerPixel = 0.05;
    double Coarse = 1.0;
};

// One step a pixel where the property names its step, a two-hundredth of its
// bounds where it has bounds, otherwise a twentieth of a unit; coarse steps
// are a twentieth of the bounds, five steps, or whole units.
[[nodiscard]] ScrubScale ScrubScaleFor(const PropertyDescriptor& descriptor);

class ScrubGesture
{
public:
    static constexpr double kDeadZonePixels = 3.0;

    explicit ScrubGesture(ScrubScale scale)
        : Scale(scale)
    {
    }

    // The pointer moved across by `pixels`; whether this is a drag now.
    bool Move(double pixels, bool fine);
    [[nodiscard]] bool Dragging() const { return Dragged; }
    // How far a number that began at `start` has moved, in coarse steps when
    // `coarse`: the value it lands on is a whole number of them.
    [[nodiscard]] double Amount(double start, bool coarse) const;

private:
    ScrubScale Scale;
    double Travel = 0.0;
    double Moved = 0.0;
    bool Dragged = false;
};
