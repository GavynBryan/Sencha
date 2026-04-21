#include "SelectionService.h"

SelectionService::SelectionService(ISelectionContext& context)
    : Context(context)
{
}

SelectableRef SelectionService::GetPrimarySelection() const
{
    return Context.GetPrimarySelection();
}

void SelectionService::ApplySelection(SelectableRef selection)
{
    Context.SetPrimarySelection(selection);
    Notify(selection);
}

void SelectionService::ClearSelection()
{
    Context.SetPrimarySelection({});
    Notify({});
}

ISelectionContext& SelectionService::GetContext()
{
    return Context;
}

const ISelectionContext& SelectionService::GetContext() const
{
    return Context;
}

std::shared_ptr<SelectionService::ObserverFn> SelectionService::Subscribe(ObserverFn fn)
{
    auto ptr = std::make_shared<ObserverFn>(std::move(fn));
    Observers.push_back(ptr);
    return ptr;
}

void SelectionService::Notify(SelectableRef selection)
{
    auto it = Observers.begin();
    while (it != Observers.end())
    {
        if (auto fn = it->lock())
        {
            (*fn)(selection);
            ++it;
        }
        else
        {
            it = Observers.erase(it);
        }
    }
}
