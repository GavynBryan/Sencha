#pragma once

#include "TransformMode.h"

// Remembers the last transform mode chosen in each editing context (object vs
// mesh-element), so switching element kind restores the gizmo the user was
// working with there instead of carrying one context's choice into the other.
// Resize only applies to whole objects, so it is never recorded for the element
// context (the element default stays a usable gizmo).
struct TransformModeMemory
{
    TransformMode ObjectMode = TransformMode::Resize;
    TransformMode ElementMode = TransformMode::Move;

    [[nodiscard]] TransformMode ModeFor(bool elementContext) const
    {
        return elementContext ? ElementMode : ObjectMode;
    }

    void Record(bool elementContext, TransformMode mode)
    {
        if (elementContext)
        {
            if (mode != TransformMode::Resize)
                ElementMode = mode;
        }
        else
        {
            ObjectMode = mode;
        }
    }
};
