#pragma once

#include "ISelectionContext.h"

class SelectionContext : public ISelectionContext
{
public:
    std::span<const SelectableRef> GetSelection() const override;
    SelectableRef GetPrimarySelection() const override;
    bool Contains(SelectableRef selection) const override;
    SelectionSnapshot GetSnapshot() const override;
    void SetSnapshot(SelectionSnapshot snapshot) override;

private:
    std::vector<SelectableRef> Selection;
    SelectableRef PrimarySelection = {};
};
