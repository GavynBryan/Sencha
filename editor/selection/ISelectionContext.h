#pragma once

#include "SelectableRef.h"

struct ISelectionContext
{
    virtual SelectableRef GetPrimarySelection() const = 0;
    virtual void SetPrimarySelection(SelectableRef selection) = 0;
    virtual ~ISelectionContext() = default;
};
