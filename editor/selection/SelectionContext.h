#pragma once

#include "ISelectionContext.h"

class SelectionContext : public ISelectionContext
{
public:
    SelectableRef GetPrimarySelection() const override;
    void SetPrimarySelection(SelectableRef selection) override;

private:
    SelectableRef PrimarySelection = {};
};
