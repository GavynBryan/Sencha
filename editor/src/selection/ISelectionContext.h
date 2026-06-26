#pragma once

#include "SelectableRef.h"

#include <span>
#include <vector>

struct SelectionSnapshot
{
    std::vector<SelectableRef> Items;
    SelectableRef Primary = {};
};

struct ISelectionContext
{
    virtual std::span<const SelectableRef> GetSelection() const = 0;
    virtual SelectableRef GetPrimarySelection() const = 0;
    virtual bool Contains(SelectableRef selection) const = 0;
    virtual SelectionSnapshot GetSnapshot() const = 0;
    virtual void SetSnapshot(SelectionSnapshot snapshot) = 0;
    virtual ~ISelectionContext() = default;
};
