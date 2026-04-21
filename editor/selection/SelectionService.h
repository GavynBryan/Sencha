#pragma once

#include "ISelectionContext.h"

class SelectionService
{
public:
    explicit SelectionService(ISelectionContext& context);

    [[nodiscard]] SelectableRef GetPrimarySelection() const;
    void ApplySelection(SelectableRef selection);
    void ClearSelection();

    [[nodiscard]] ISelectionContext& GetContext();
    [[nodiscard]] const ISelectionContext& GetContext() const;

private:
    ISelectionContext& Context;
};
