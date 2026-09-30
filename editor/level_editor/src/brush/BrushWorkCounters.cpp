#include "BrushWorkCounters.h"

namespace
{
    BrushWorkCounters gFrame;
    BrushWorkCounters gLast;
}

const BrushWorkCounters& BrushWorkCounters::LastFrame()
{
    return gLast;
}

BrushWorkCounters& BrushWorkCounters::Frame()
{
    return gFrame;
}

BrushWorkCounters BrushWorkCounters::TakeFrame()
{
    const BrushWorkCounters taken = gFrame;
    gLast = taken;
    gFrame = BrushWorkCounters{};
    return taken;
}
