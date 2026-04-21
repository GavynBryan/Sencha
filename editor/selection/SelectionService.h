#pragma once

#include "ISelectionContext.h"

#include <functional>
#include <memory>
#include <vector>

class SelectionService
{
public:
    using ObserverFn = std::function<void(SelectableRef)>;

    explicit SelectionService(ISelectionContext& context);

    [[nodiscard]] SelectableRef GetPrimarySelection() const;
    void ApplySelection(SelectableRef selection);
    void ClearSelection();

    [[nodiscard]] ISelectionContext& GetContext();
    [[nodiscard]] const ISelectionContext& GetContext() const;

    [[nodiscard]] std::shared_ptr<ObserverFn> Subscribe(ObserverFn fn);

private:
    ISelectionContext& Context;
    std::vector<std::weak_ptr<ObserverFn>> Observers;

    void Notify(SelectableRef selection);
};
