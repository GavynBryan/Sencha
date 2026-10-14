#include "shell/ScrubGesture.h"

#include <cmath>

ScrubScale ScrubScaleFor(const PropertyDescriptor& descriptor)
{
    const bool bounded = descriptor.Max > descriptor.Min;
    if (descriptor.Step > 0.0)
        return { .PerPixel = descriptor.Step,
                 .Coarse = bounded ? (descriptor.Max - descriptor.Min) / 20.0 : descriptor.Step * 5.0 };
    if (bounded)
        return { .PerPixel = (descriptor.Max - descriptor.Min) / 200.0,
                 .Coarse = (descriptor.Max - descriptor.Min) / 20.0 };
    return {};
}

bool ScrubGesture::Move(double pixels, bool fine)
{
    if (!Dragged)
    {
        Travel += pixels;
        if (std::abs(Travel) < kDeadZonePixels)
            return false;
        Dragged = true;
        return true;
    }
    Moved += pixels * Scale.PerPixel * (fine ? 0.1 : 1.0);
    return true;
}

double ScrubGesture::Amount(double start, bool coarse) const
{
    if (!coarse || Scale.Coarse <= 0.0)
        return Moved;
    return std::round((start + Moved) / Scale.Coarse) * Scale.Coarse - start;
}
